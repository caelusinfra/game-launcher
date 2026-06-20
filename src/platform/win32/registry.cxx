#include "registry.hxx"
#include "str_convert.hxx"
#include <memory>
#include <stdexcept>

struct KeyDeleter {
    void operator()(HKEY h) const { if (h) RegCloseKey(h); }
};
using KeyPtr = std::unique_ptr<std::remove_pointer_t<HKEY>, KeyDeleter>;

static KeyPtr OpenOrCreate(HKEY root, const std::wstring& path, REGSAM access = KEY_READ | KEY_WRITE)
{
    HKEY h = nullptr;
    LONG r = RegCreateKeyExW(root, path.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, access, nullptr, &h, nullptr);
    if (r != ERROR_SUCCESS)
        throw std::runtime_error("RegCreateKeyEx failed for: " + std::string(path.begin(), path.end()));
    return KeyPtr(h);
}

static KeyPtr OpenExisting(HKEY root, const std::wstring& path, REGSAM access = KEY_READ)
{
    HKEY h = nullptr;
    LONG r = RegOpenKeyExW(root, path.c_str(), 0, access, &h);
    if (r != ERROR_SUCCESS) return {};
    return KeyPtr(h);
}

static std::wstring ReadWideString(HKEY key, const wchar_t* name)
{
    DWORD type = REG_SZ, size = 0;
    if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &size) != ERROR_SUCCESS) return {};
    std::wstring wide(size / sizeof(wchar_t), L'\0');
    if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(wide.data()), &size) != ERROR_SUCCESS) return {};
    while (!wide.empty() && wide.back() == L'\0') wide.pop_back();
    return wide;
}

static std::string ReadString(HKEY key, const wchar_t* name)
{
    DWORD type = REG_SZ, size = 0;
    if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &size) != ERROR_SUCCESS) return {};
    std::wstring wide(size / sizeof(wchar_t), L'\0');
    if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(wide.data()), &size) != ERROR_SUCCESS) return {};
    while (!wide.empty() && wide.back() == L'\0') wide.pop_back();
    int n = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, out.data(), n, nullptr, nullptr);
    return out;
}

static void WriteString(HKEY key, const wchar_t* name, const std::wstring& value)
{
    RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
}

RegistryManager::RegistryManager(const ProductConfig& cfg)
    : reg_root_(Utf8ToWide(cfg.reg_root))
    , etag_root_(Utf8ToWide(cfg.reg_root) + L"\\ETags")
    , scheme_(cfg.protocol)
{}

std::string RegistryManager::ReadVersion() const
{
    auto key = OpenExisting(HKEY_CURRENT_USER, reg_root_);
    if (!key) return {};
    return ReadString(key.get(), L"version");
}

void RegistryManager::WriteVersion(const std::string& version)
{
    auto key = OpenOrCreate(HKEY_CURRENT_USER, reg_root_);
    WriteString(key.get(), L"version", Utf8ToWide(version));
}

std::wstring RegistryManager::ReadBin() const
{
    auto key = OpenExisting(HKEY_CURRENT_USER, reg_root_);
    if (!key) return {};
    return ReadWideString(key.get(), L"bin");
}

void RegistryManager::WriteBin(const std::wstring& bin)
{
    auto key = OpenOrCreate(HKEY_CURRENT_USER, reg_root_);
    WriteString(key.get(), L"bin", bin);
}

std::string RegistryManager::ReadETag(const std::wstring& filename) const
{
    auto key = OpenExisting(HKEY_CURRENT_USER, etag_root_);
    if (!key) return {};
    return ReadString(key.get(), filename.c_str());
}

void RegistryManager::WriteETag(const std::wstring& filename, const std::string& etag)
{
    auto key = OpenOrCreate(HKEY_CURRENT_USER, etag_root_);
    WriteString(key.get(), filename.c_str(), Utf8ToWide(etag));
}

void RegistryManager::RegisterProtocol(const std::wstring& bootstrapper)
{
    std::wstring scheme_root = L"Software\\Classes\\" + scheme_;
    {
        auto key = OpenOrCreate(HKEY_CURRENT_USER, scheme_root);
        WriteString(key.get(), nullptr, L"URL:" + scheme_ + L" Protocol");
        WriteString(key.get(), L"URL Protocol", L"");
    }
    auto cmd_key = OpenOrCreate(HKEY_CURRENT_USER, scheme_root + L"\\shell\\open\\command");
    WriteString(cmd_key.get(), nullptr, L"\"" + bootstrapper + L"\" \"%1\"");
}

void RegistryManager::RegisterUninstall(const std::wstring& bootstrapper)
{
    const std::wstring uninstall_root =
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Aisaka Unincorporation";
    auto key = OpenOrCreate(HKEY_CURRENT_USER, uninstall_root);
    WriteString(key.get(), L"DisplayName", L"Aisaka");
    WriteString(key.get(), L"UninstallString", L"\"" + bootstrapper + L"\" --uninstall");
    WriteString(key.get(), L"Publisher", L"Aisaka Unincorporation");
    WriteString(key.get(), L"NoModify", L"1");
    WriteString(key.get(), L"NoRepair", L"1");
}

void RegistryManager::DeleteKeys()
{
    RegDeleteTreeW(HKEY_CURRENT_USER, reg_root_.c_str());

    std::wstring scheme_root = L"Software\\Classes\\" + scheme_;
    RegDeleteTreeW(HKEY_CURRENT_USER, scheme_root.c_str());

    RegDeleteTreeW(HKEY_CURRENT_USER,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Aisaka Unincorporation");
}
