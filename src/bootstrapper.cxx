#include "bootstrapper.hxx"
#include "file_deployer.hxx"
#include "registry.hxx"
#include "app_settings.hxx"
#include "shortcut.hxx"
#include "locales.hxx"
#include "str_convert.hxx"
#include "http_client.hxx"
#include <shlobj.h>
#include <filesystem>
#include <stdexcept>

namespace fs = std::filesystem;

static std::wstring GetKnownFolder(REFKNOWNFOLDERID id)
{
    wchar_t* p = nullptr;
    SHGetKnownFolderPath(id, 0, nullptr, &p);
    std::wstring result = p ? std::wstring(p) : L"";
    CoTaskMemFree(p);
    return result;
}

static std::wstring BuildArgs(const PlayArgs& args)
{
    std::wstring cmd;
    cmd += L"--play";
    cmd += L" -a " + Utf8ToWide(args.auth_url);
    cmd += L" -t " + Utf8ToWide(args.auth_ticket);
    cmd += L" -j " + Utf8ToWide(args.place_launcher_url);
    return cmd;
}

static void Launch(const std::wstring& bootstrapper, const std::wstring& extra_args = L"")
{
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + bootstrapper + L"\"";
    if (!extra_args.empty()) cmd += L" " + extra_args;
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        DWORD err = GetLastError();
        throw std::runtime_error("Failed to launch " + std::string(bootstrapper.begin(), bootstrapper.end()) + " (" + std::to_string(err) + ")");
    }

	Sleep(5000);

    if (pi.hProcess) CloseHandle(pi.hProcess);
    if (pi.hThread) CloseHandle(pi.hThread);
}

std::string FetchVersion(const std::string& cdn_url, const std::string& version)
{
    std::string url = cdn_url + "/" + version;

    std::string body, etag;
    int status = HttpGet(url, {}, body, etag);
    if (status != 200 || body.empty())
        throw std::runtime_error("Failed to get version (" + std::to_string(status) + ")");

    while (!body.empty() && (body.back() == '\r' || body.back() == '\n' || body.back() == ' '))
        body.pop_back();

    return body;
}

void RunBootstrap(const ProductConfig& cfg, const std::string& cdn_url, const std::string& base_url, const std::wstring& install_dir, const std::wstring& bootstrapper, const PlayArgs* play_args, StatusCallback status_cb, const std::atomic<bool>& cancelled)
{
    auto& locale = LocaleManager::getInstance();
    RegistryManager reg(cfg);

    auto report = [&](const std::string& key, int pct) {
        if (status_cb)
            status_cb({ locale.getLocalizedString(key), pct, false, {} });
    };
    auto report_err = [&](const std::wstring& msg) {
        if (status_cb) status_cb({ {}, 0, false, msg });
    };

    auto copy_self = [&]() {
        wchar_t self_path[MAX_PATH]{};
        GetModuleFileNameW(nullptr, self_path, MAX_PATH);
        if (_wcsicmp(self_path, bootstrapper.c_str()) != 0) {
            std::error_code ec;
            fs::copy_file(self_path, bootstrapper, fs::copy_options::update_existing, ec);
        }
    };

    auto kill_me_please = [&](const std::wstring& bin) {
        bool tray = play_args != nullptr || cfg.type == "WindowsStudio";
        if (play_args)
            Launch(bin, BuildArgs(*play_args));
        else
            Launch(bin);
        if (status_cb) status_cb({ {}, 100, false, {}, true, false, tray });
    };

    try {
        report("connecting", -1);
        if (cancelled.load()) return;

        std::string current_version;
        try {
            current_version = FetchVersion(cdn_url, cfg.version);
        } catch (...) {
            current_version = FetchVersion(cdn_url, cfg.version);
        }

        if (cancelled.load()) return;

        Manifest manifest = FetchManifest(cdn_url, cfg.type, current_version);
        std::string installed = reg.ReadVersion();
        bool first_install = installed.empty();
        std::wstring version = install_dir + L"\\Versions\\" + Utf8ToWide(current_version);
        std::wstring bin = version + L"\\" + manifest.bin;

        if (!first_install && installed == current_version) {
            report("starting", -1);
            copy_self();
            reg.RegisterProtocol(bootstrapper);
            kill_me_please(bin);
            return;
        }

        if (status_cb) status_cb({ {}, -1, false, {}, false, true });
        report("getting_latest", 0);
        fs::create_directories(version);

        const std::string progress_key = first_install ? "installing" : "upgrading";
        DeployComponents(cdn_url, cfg.type, current_version, manifest.files, version, reg,
            [&](int pct, const std::wstring& msg) {
                if (status_cb) {
                    std::wstring text = (pct > 0) ? locale.getLocalizedString(progress_key) : L"";
                    status_cb({ text, pct, false, {} });
                }
            },
            cancelled);

        if (cancelled.load()) return;

        copy_self();
        reg.WriteVersion(current_version);
        reg.WriteBin(manifest.bin);
        reg.RegisterProtocol(bootstrapper);
        reg.RegisterUninstall(bootstrapper);

        Configure(version, base_url);

        if (cfg.type == "WindowsPlayer" && first_install) {
            std::wstring desktop = GetKnownFolder(FOLDERID_Desktop);
            std::wstring start = GetKnownFolder(FOLDERID_Programs) + L"\\Aisaka";
            fs::create_directories(start);

            AddShortcut(bootstrapper, L"", cfg.name, desktop);
            AddShortcut(bootstrapper, L"--studio", L"Aisaka Studio", desktop);
            AddShortcut(bootstrapper, L"", cfg.name, start);
            AddShortcut(bootstrapper, L"--studio", L"Aisaka Studio", start);
        }

        if (!installed.empty() && installed != current_version) {
            std::wstring old = install_dir + L"\\Versions\\" + Utf8ToWide(installed);
            std::error_code ec;
            fs::remove_all(old, ec);
        }

        if (first_install) {
            if (status_cb) status_cb({ {}, 100, true, {} });
        } else {
            report("starting", -1);
            kill_me_please(bin);
        }

    } catch (const std::exception& e) {
        report_err(Utf8ToWide(std::string("Error: ") + e.what()));
    } catch (...) {
        report_err(L"An unexpected error occurred.");
    }
}
