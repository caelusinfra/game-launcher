#include "file_deployer.hxx"
#include "http_client.hxx"
#include "str_convert.hxx"
#include <windows.h>
#include <shlobj.h>
#include <archive.h>
#include <archive_entry.h>
#include <sstream>
#include <stdexcept>
#include <filesystem>

namespace fs = std::filesystem;

static std::wstring GetDownloadsCacheDir()
{
    wchar_t* appdata = nullptr;
    SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &appdata);
    std::wstring base = appdata ? std::wstring(appdata) : L"C:\\Temp";
    CoTaskMemFree(appdata);
    base += L"\\Aisaka\\Downloads";
    fs::create_directories(base);
    return base;
}

Manifest FetchManifest(const std::string& cdn_url, const std::string& channel, const std::string& version)
{
    std::string url = cdn_url + "/" + channel + "/" + version + "/manifest.txt";
    std::string body, etag;
    int status = HttpGet(url, {}, body, etag);
    if (status != 200 || body.empty())
        throw std::runtime_error("Failed to get manifest (" + std::to_string(status) + ")");

    Manifest m;
    std::istringstream ss(body);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (line.size() > 4 && line.substr(0, 4) == "bin:") {
            m.bin = Utf8ToWide(line.substr(4));
        } else {
            m.files.push_back(Utf8ToWide(line));
        }
    }

    if (m.bin.empty())
        throw std::runtime_error("Manifest is invalid");
    if (m.files.empty())
        throw std::runtime_error("Manifest has no files");

    return m;
}

std::wstring DownloadVersionedFile(const std::string& cdn_url, const std::string& type, const std::string& version, const std::wstring& name, RegistryManager& registry_mgr, ProgressCallback cb, const std::atomic<bool>& cancelled)
{
    std::string url = cdn_url + "/" + type + "/" + version + "/" + WideToUtf8(name);
    std::string cached_etag = registry_mgr.ReadETag(name);

    std::wstring cache = GetDownloadsCacheDir();
    std::wstring tmp_path = cache + L"\\" + name + L".tmp";

    std::string new_etag;
    int status = HttpDownload(url, cached_etag, tmp_path, new_etag,
        [&](int64_t dl, int64_t total) {
            if (total > 0 && cb)
                cb(static_cast<int>((dl * 100) / total), L"");
        },
        cancelled);

    if (status == 0) return {};
    if (status == 304) {
        DeleteFileW(tmp_path.c_str());
        return cache + L"\\" + name;
    }
    if (status != 200)
        throw std::runtime_error("Download failed for " + WideToUtf8(name) + " (" + std::to_string(status) + ")");

    std::wstring final_path = cache + L"\\" + name;
    DeleteFileW(final_path.c_str());
    MoveFileW(tmp_path.c_str(), final_path.c_str());

    if (!new_etag.empty())
        registry_mgr.WriteETag(name, new_etag);

    return final_path;
}

void Extract(const std::wstring& archive_path, const std::wstring& dest_dir)
{
    struct archive* a = archive_read_new();
    archive_read_support_format_all(a);
    archive_read_support_filter_all(a);

    struct archive* ext = archive_write_disk_new();
    archive_write_disk_set_options(ext, ARCHIVE_EXTRACT_TIME);
    archive_write_disk_set_standard_lookup(ext);

    if (archive_read_open_filename_w(a, archive_path.c_str(), 65536) != ARCHIVE_OK) {
        std::string err = archive_error_string(a);
        archive_read_free(a);
        archive_write_free(ext);
        throw std::runtime_error("Failed to open archive: " + err);
    }

    struct archive_entry* entry;
    int r;
    while ((r = archive_read_next_header(a, &entry)) == ARCHIVE_OK) {
        std::wstring name;
        if (const wchar_t* wp = archive_entry_pathname_w(entry))
            name = wp;
        else if (const char* p = archive_entry_pathname(entry))
            name = Utf8ToWide(p);
        else { archive_read_data_skip(a); continue; }

        for (auto& c : name) if (c == L'/') c = L'\\';
        std::wstring full = dest_dir + L"\\" + name;
        archive_entry_copy_pathname_w(entry, full.c_str());

        archive_write_header(ext, entry);

        const void* buf;
        size_t size;
        la_int64_t offset;
        while (archive_read_data_block(a, &buf, &size, &offset) == ARCHIVE_OK)
            archive_write_data_block(ext, buf, size, offset);

        archive_write_finish_entry(ext);
    }

    archive_read_free(a);
    archive_write_free(ext);

    if (r != ARCHIVE_EOF)
        throw std::runtime_error("Extraction failed");
}

void DeployComponents(const std::string& cdn_url, const std::string& channel, const std::string& version, const std::vector<std::wstring>& files, const std::wstring& install_dir, RegistryManager& registry_mgr, ProgressCallback cb, const std::atomic<bool>& cancelled)
{
    if (files.empty())
        throw std::runtime_error("Nothing to deploy");

    int total = static_cast<int>(files.size());
    for (int idx = 0; idx < total; ++idx) {
        if (cancelled.load()) return;

        const std::wstring& name = files[idx];
        int base_pct = (idx * 80) / total;
        int next_pct = ((idx + 1) * 80) / total;

        if (cb) cb(base_pct, L"");

        std::wstring zip = DownloadVersionedFile(cdn_url, channel, version, name, registry_mgr,
            [&](int p, const std::wstring&) {
                if (cb) cb(base_pct + p * (next_pct - base_pct) / 100, L"");
            },
            cancelled);

        if (zip.empty()) return;

        if (cb) cb(next_pct - 1, L"");
        Extract(zip, install_dir);
        if (cb) cb(next_pct, L"");
    }
}