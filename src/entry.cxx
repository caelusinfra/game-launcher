#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <thread>
#include <atomic>
#include <filesystem>
#include <optional>
#include <map>
#include <string>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "platform/main_dialog.hxx"
#include "locales.hxx"
#include "product_config.hxx"
#include "bootstrapper.hxx"
#include "registry.hxx"
#include "http_client.hxx"
#include "str_convert.hxx"
#include "framerate.hxx"
#include "rpc.hxx"
#include "file_deployer.hxx"

namespace fs = std::filesystem;

const std::string cdn_url = "https://setup.clscdn.lol";
const std::string base_url = "https://www.aisaka.me";

static std::string UrlDecode(const std::string& in)
{
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ) {
        if (in[i] == '%' && i + 2 < in.size()) {
            int hi = in[i + 1], lo = in[i + 2];
            auto hex = [](int c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                return -1;
            };
            int h = hex(hi), l = hex(lo);
            if (h >= 0 && l >= 0) {
                out += static_cast<char>(h * 16 + l);
                i += 3;
                continue;
            }
        }
        if (in[i] == '+') {
            out += ' ';
            ++i;
        } else {
            out += in[i++];
        }
    }
    return out;
}

static std::optional<PlayArgs> ReadJoinScript(const wchar_t* lpCmdLine)
{
    if (!lpCmdLine || !*lpCmdLine) return std::nullopt;
    std::wstring raw = lpCmdLine;

    while (!raw.empty() && raw.front() == L'"') raw.erase(0, 1);
    while (!raw.empty() && raw.back() == L'"') raw.pop_back();
    while (!raw.empty() && raw.front() == L' ') raw.erase(0, 1);
    while (!raw.empty() && raw.back() == L' ') raw.pop_back();

    if (raw.find(L"://") == std::wstring::npos)
        return std::nullopt;

    std::string narrow = WideToUtf8(raw);

    std::map<std::string, std::string> params;
    size_t pos = 0;
    while (pos <= narrow.size()) {
        size_t next = narrow.find('+', pos);
        if (next == std::string::npos) next = narrow.size();

        std::string seg = narrow.substr(pos, next - pos);
        pos = next + 1;

        if (seg.empty()) continue;

        auto colon = seg.find(':');
        if (colon == std::string::npos) continue;

        std::string key = seg.substr(0, colon);
        std::string val = UrlDecode(seg.substr(colon + 1));

        for (auto& c : key)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        params[key] = val;
    }

    auto it_mode = params.find("launchmode");
    if (it_mode == params.end() || it_mode->second != "play")
        return std::nullopt;

    auto it_url = params.find("placelauncherurl");
    auto it_ticket = params.find("gameinfo");
    if (it_url == params.end() || it_ticket == params.end())
        return std::nullopt;

    PlayArgs args;
    args.place_launcher_url = it_url->second;
    args.auth_ticket = it_ticket->second;
    args.auth_url = "https://auth.aisaka.me/v1/authentication-ticket/redeem";

    return args;
}

static std::wstring GetInstallDir(const ProductConfig& cfg)
{
    wchar_t* appdata = nullptr;
    SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &appdata);
    std::wstring base = appdata ? std::wstring(appdata) : L"C:\\Temp";
    CoTaskMemFree(appdata);

    return base + L"\\Aisaka";
}

static std::wstring GetDesktop()
{
    wchar_t path[MAX_PATH] = {};
    SHGetFolderPathW(nullptr, CSIDL_DESKTOP, nullptr, 0, path);
    return path;
}

static void RunUninstall(const std::wstring& install_dir, const std::wstring& bootstrapper)
{
    RegistryManager(GetProductConfig(nullptr)).DeleteKeys();
    RegistryManager(GetProductConfig(L"--studio")).DeleteKeys();

    std::wstring desktop = GetDesktop();
    DeleteFileW((desktop + L"\\Aisaka Player.lnk").c_str());
    DeleteFileW((desktop + L"\\Aisaka Studio.lnk").c_str());

    wchar_t* programs_path = nullptr;
    SHGetKnownFolderPath(FOLDERID_Programs, 0, nullptr, &programs_path);
    if (programs_path) {
        std::wstring start_folder = std::wstring(programs_path) + L"\\Aisaka";
        CoTaskMemFree(programs_path);
        std::error_code ec2;
        fs::remove_all(start_folder, ec2);
    }

    std::error_code ec;
    for (auto& e : fs::directory_iterator(install_dir, ec)) {
        if (e.path() == fs::path(bootstrapper)) continue;
        fs::remove_all(e.path(), ec);
    }

    MoveFileExW(bootstrapper.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    MoveFileExW(install_dir.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);

    MessageBoxW(nullptr,
        L"Aisaka has been uninstalled, although some files will not be removed until you reboot.",
        L"Uninstalled", MB_OK | MB_ICONINFORMATION);
}

static bool HasFlag(const wchar_t* lpCmdLine, const wchar_t* flag)
{
    return lpCmdLine && wcsstr(lpCmdLine, flag) != nullptr;
}

#define WM_TRAYICON (WM_APP + 1)
#define WM_ALL_EXITED (WM_APP + 2)
#define WM_TRAY_ATTACH (WM_APP + 3)
#define WM_TYPE_CHANGE (WM_APP + 4)
#define IDM_FCS_BASE (WM_APP + 10)

static const double kFramerateCaps[] = { 0, 240, 144, 120, 75, 60, 30 };
static const wchar_t* kFramerateLabels[] = { L"Unlimited", L"240", L"144", L"120", L"75", L"60", L"30" };
static constexpr int kFramerateCount = 7;
static int s_framerate_sel = 2;

static NOTIFYICONDATA s_nid{};
static HANDLE s_watch_thread = nullptr;
static HINSTANCE s_hInst = nullptr;
static double s_custom_framerate = 0.0;
static bool s_dlg_done = false;
static double s_dlg_result = -1.0;
static constexpr int kFramerateCustom = 7;

static long long s_player_place_id = -1;
static bool s_studio_active = false;

static void UpdateRpc()
{
    Rpc::Stop();
    if (s_player_place_id >= 0)
        Rpc::Start(s_player_place_id);
    else if (s_studio_active)
        Rpc::Start(0);
}

static void GetConfig(wchar_t* out, DWORD size)
{
    wchar_t* appdata = nullptr;
    SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &appdata);
    swprintf_s(out, size, L"%s\\Aisaka\\config", appdata ? appdata : L"C:\\Temp");
    CoTaskMemFree(appdata);
}

static void SaveConfig()
{
    wchar_t path[MAX_PATH]{};
    GetConfig(path, MAX_PATH);
    double cap = (s_framerate_sel < kFramerateCount) ? kFramerateCaps[s_framerate_sel] : s_custom_framerate;
    FILE* f = nullptr;
    _wfopen_s(&f, path, L"w");
    if (!f) return;
    fprintf(f, "FramerateCap=%.6f\n", cap);
    fclose(f);
}

static void LoadConfig()
{
    wchar_t path[MAX_PATH]{};
    GetConfig(path, MAX_PATH);
    FILE* f = nullptr;
    _wfopen_s(&f, path, L"r");
    if (!f) return;
    char buf[256]{};
    fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    const char* pc = strstr(buf, "FramerateCap=");
    if (!pc) return;
    double cap = atof(pc + 13);
    for (int i = 0; i < kFramerateCount; ++i) {
        if (kFramerateCaps[i] == cap) {
            s_framerate_sel = i;
            return;
        }
    }
    s_framerate_sel = kFramerateCustom;
    s_custom_framerate = cap;
}

static LRESULT CALLBACK CustomFramerateWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        auto add = [&](const wchar_t* cls, const wchar_t* txt, DWORD style, DWORD exstyle, int x, int y, int w, int h, int id) -> HWND {
            HWND c = CreateWindowExW(exstyle, cls, txt, WS_CHILD | WS_VISIBLE | style, x, y, w, h, hwnd, (HMENU)(INT_PTR)id, s_hInst, nullptr);
            SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE);
            return c;
        };
        HICON hIcon = LoadIconW(s_hInst, MAKEINTRESOURCEW(IDI_BOOTSTRAPPER));
        SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
        add(L"STATIC", L"Custom Framerate:", SS_LEFT, 0, 10, 12, 220, 18, 0);
        HWND hEdit = add(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, 10, 35, 220, 22, 101);
        add(L"BUTTON", L"OK", BS_DEFPUSHBUTTON | WS_TABSTOP, 0, 10, 68, 104, 26, IDOK);
        add(L"BUTTON", L"Cancel", WS_TABSTOP, 0, 120, 68, 104, 26, IDCANCEL);
        auto* init = (double*)((CREATESTRUCT*)lp)->lpCreateParams;
        if (init && *init > 0.0) {
            wchar_t buf[32]{};
            swprintf_s(buf, L"%.0f", *init);
            SetWindowTextW(hEdit, buf);
            SendMessageW(hEdit, EM_SETSEL, 0, -1);
        }
        SetFocus(hEdit);
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id == IDOK) {
            wchar_t buf[32]{};
            GetDlgItemTextW(hwnd, 101, buf, 32);
            wchar_t* end = nullptr;
            double val = wcstod(buf, &end);
            if (end != buf && val >= 0.0) {
                s_dlg_result = val;
                s_dlg_done = true;
                DestroyWindow(hwnd);
            }
        } else if (id == IDCANCEL) {
            s_dlg_done = true;
            DestroyWindow(hwnd);
        }
        return 0;
    }
    case WM_CLOSE:
        s_dlg_done = true;
        DestroyWindow(hwnd);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

static double ShowFramerateDialog(double init_cap)
{
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = CustomFramerateWndProc;
    wc.hInstance = s_hInst;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"AisakaCustomFramerate";
    RegisterClassExW(&wc);

    s_dlg_done = false;
    s_dlg_result = -1.0;

    if (init_cap <= 0.0) {
        HDC hdc = GetDC(nullptr);
        int hz = GetDeviceCaps(hdc, VREFRESH);
        ReleaseDC(nullptr, hdc);
        if (hz > 0) init_cap = static_cast<double>(hz);
    }

    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, L"AisakaCustomFramerate", L"Aisaka", WS_POPUP | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 252, 136, nullptr, nullptr, s_hInst, &init_cap);

    if (!dlg) return -1.0;

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    RECT rc{};
    GetWindowRect(dlg, &rc);
    SetWindowPos(dlg, nullptr, (sw - (rc.right - rc.left)) / 2, (sh - (rc.bottom - rc.top)) / 2, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

    ShowWindow(dlg, SW_SHOW);
    UpdateWindow(dlg);

    MSG msg;
    int ret = 1;
    while (!s_dlg_done && (ret = GetMessageW(&msg, nullptr, 0, 0)) > 0) {
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (ret == 0) PostQuitMessage(0);

    return s_dlg_result;
}

static LRESULT CALLBACK TrayWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_TRAY_ATTACH) {
        bool is_studio = (wp == 1);
        if (is_studio) s_studio_active = true;
        else s_player_place_id = (long long)lp;
        {
            std::vector<std::wstring> pn, sn;
            std::wstring pe = RegistryManager(GetProductConfig(nullptr)).ReadBin();
            std::wstring se = RegistryManager(GetProductConfig(L"--studio")).ReadBin();
            if (!pe.empty()) pn.push_back(pe);
            if (!se.empty()) sn.push_back(se);
            Framerate::SetProcesses(pn, sn);
        }
        UpdateRpc();
        return 0;
    }

    if (msg == WM_TYPE_CHANGE) {
        uint32_t mask = (uint32_t)wp;
        bool player_now = (mask & 1) != 0;
        bool studio_now = (mask & 2) != 0;
        if (!player_now && s_player_place_id >= 0) {
            s_player_place_id = -1;
            UpdateRpc();
        }
        if (!studio_now && s_studio_active) {
            s_studio_active = false;
            UpdateRpc();
        }
        return 0;
    }

    if (msg == WM_ALL_EXITED) {
        Shell_NotifyIconW(NIM_DELETE, &s_nid);
        Framerate::StopWatchThread(s_watch_thread);
        Rpc::Stop();
        PostQuitMessage(0);
        return 0;
    }

    if (msg == WM_TRAYICON && (lp == WM_RBUTTONDOWN || lp == WM_LBUTTONDOWN)) {
        POINT pt{};
        GetCursorPos(&pt);

        HMENU sub = CreatePopupMenu();
        for (int i = 0; i < kFramerateCount; ++i)
            AppendMenuW(sub, MF_STRING, IDM_FCS_BASE + i, kFramerateLabels[i]);
        wchar_t custom_label[64]{};
        if (s_framerate_sel == kFramerateCustom && s_custom_framerate > 0.0)
            swprintf_s(custom_label, L"Custom (%.0f)", s_custom_framerate);
        else
            wcscpy_s(custom_label, L"Custom...");
        AppendMenuW(sub, MF_STRING, IDM_FCS_BASE + kFramerateCustom, custom_label);
        CheckMenuRadioItem(sub, IDM_FCS_BASE, IDM_FCS_BASE + kFramerateCustom, IDM_FCS_BASE + s_framerate_sel, MF_BYCOMMAND);

        HMENU popup = CreatePopupMenu();
        AppendMenuW(popup, MF_POPUP, (UINT_PTR)sub, L"Framerate");

        SetForegroundWindow(hwnd);
        int cmd = TrackPopupMenu(popup, TPM_RETURNCMD | TPM_BOTTOMALIGN | TPM_RIGHTALIGN, pt.x, pt.y, 0, hwnd, nullptr);
        DestroyMenu(popup);

        if (cmd >= IDM_FCS_BASE && cmd < IDM_FCS_BASE + kFramerateCount) {
            s_framerate_sel = cmd - IDM_FCS_BASE;
            Framerate::SetCap(kFramerateCaps[s_framerate_sel]);
            SaveConfig();
        } else if (cmd == IDM_FCS_BASE + kFramerateCustom) {
            double result = ShowFramerateDialog(s_framerate_sel == kFramerateCustom ? s_custom_framerate : 0.0);
            if (result >= 0.0) {
                s_framerate_sel = kFramerateCustom;
                s_custom_framerate = result;
                Framerate::SetCap(s_custom_framerate);
                SaveConfig();
            }
        }
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void RunTrayLoop(HINSTANCE hInst, long long place_id = 0)
{
    s_hInst = hInst;

    {
        HDC hdc = GetDC(nullptr);
        int hz = GetDeviceCaps(hdc, VREFRESH);
        ReleaseDC(nullptr, hdc);
        if (hz > 0) {
            bool found = false;
            for (int i = 0; i < kFramerateCount; ++i) {
                if (kFramerateCaps[i] == static_cast<double>(hz)) { s_framerate_sel = i; found = true; break; }
            }
            if (!found) { s_framerate_sel = kFramerateCustom; s_custom_framerate = static_cast<double>(hz); }
        }
    }

    LoadConfig();

    {
        HWND prev = FindWindowW(L"AisakaTrayClass", nullptr);
        if (prev) {
            PostMessageW(prev, WM_TRAY_ATTACH, place_id == 0 ? 1 : 0, (LPARAM)place_id);
            return;
        }
    }

    if (place_id == 0) s_studio_active = true;
    else s_player_place_id = place_id;

    {
        std::vector<std::wstring> pn, sn;
        std::wstring pe = RegistryManager(GetProductConfig(nullptr)).ReadBin();
        std::wstring se = RegistryManager(GetProductConfig(L"--studio")).ReadBin();
        if (!pe.empty()) pn.push_back(pe);
        if (!se.empty()) sn.push_back(se);
        Framerate::SetProcesses(pn, sn);
    }

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = TrayWndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"AisakaTrayClass";
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, L"AisakaTrayClass", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, hInst, nullptr);
    if (!hwnd) return;

    s_nid = {};
    s_nid.cbSize = sizeof(s_nid);
    s_nid.hWnd = hwnd;
    s_nid.uID = 1;
    s_nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    s_nid.uCallbackMessage = WM_TRAYICON;
    s_nid.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_BOOTSTRAPPER));
    wcscpy_s(s_nid.szTip, L"Aisaka");
    Shell_NotifyIconW(NIM_ADD, &s_nid);

    Framerate::SetExitTarget(hwnd, WM_ALL_EXITED);
    Framerate::SetTypeChangeTarget(hwnd, WM_TYPE_CHANGE);
    Framerate::SetCap(s_framerate_sel < kFramerateCount ? kFramerateCaps[s_framerate_sel] : s_custom_framerate);
    s_watch_thread = Framerate::StartWatchThread();

    UpdateRpc();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

static bool CheckBootstrapperUpdate(const std::wstring& install_dir, const std::wstring& bootstrapper, const wchar_t* lpCmdLine, StatusCallback status_cb, const std::atomic<bool>& cancelled)
{
    ProductConfig cfg = GetBootstrapperConfig();
    RegistryManager reg(cfg);

    if (status_cb)
        status_cb({ LocaleManager::getInstance().getLocalizedString("connecting"), -1, false, {} });

    std::string installed = reg.ReadVersion();
    std::string latest;
    try { latest = FetchVersion(cdn_url, cfg.version); }
    catch (...) { return false; }
    if (latest.empty() || latest == installed) return false;
    if (cancelled.load()) return false;

    auto& locale = LocaleManager::getInstance();
    std::wstring getting_msg = locale.getLocalizedString("getting_latest");
    std::wstring upgrading_msg = locale.getLocalizedString("upgrading");
    if (status_cb) status_cb({ getting_msg, 0, false, {} });

    std::wstring archive = DownloadVersionedFile(cdn_url, cfg.type, latest, L"Bootstrapper.7z", reg,
        [&](int pct, const std::wstring&) {
            if (status_cb) status_cb({ pct > 0 ? upgrading_msg : getting_msg, pct, false, {} });
        },
        cancelled);
    if (archive.empty()) return false;

    std::wstring downloads = install_dir + L"\\Downloads";
    std::wstring update_bin = downloads + L"\\AisakaLauncher.exe";

    try { Extract(archive, downloads); }
    catch (...) { return false; }
    if (!fs::exists(update_bin)) return false;

    wchar_t self_path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, self_path, MAX_PATH);
    bool in_place = (_wcsicmp(self_path, bootstrapper.c_str()) == 0);

    if (in_place) {
        std::wstring old_bin = bootstrapper + L".old";
        if (!MoveFileExW(bootstrapper.c_str(), old_bin.c_str(), MOVEFILE_REPLACE_EXISTING))
            return false;
        if (!MoveFileExW(update_bin.c_str(), bootstrapper.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            MoveFileExW(old_bin.c_str(), bootstrapper.c_str(), MOVEFILE_REPLACE_EXISTING);
            return false;
        }
        MoveFileExW(old_bin.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    } else {
        if (!MoveFileExW(update_bin.c_str(), bootstrapper.c_str(), MOVEFILE_REPLACE_EXISTING))
            return false;
    }
    reg.WriteVersion(latest);

    std::wstring cmd = L"\"" + bootstrapper + L"\"";
    if (lpCmdLine && *lpCmdLine) { cmd += L" "; cmd += lpCmdLine; }
    STARTUPINFOW si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi);
    if (pi.hProcess) CloseHandle(pi.hProcess);
    if (pi.hThread) CloseHandle(pi.hThread);

    if (status_cb) status_cb({ {}, 100, false, {}, true, false, false });
    return true;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR lpCmdLine, int)
{
    HttpGlobalInit();

    ProductConfig cfg = GetProductConfig(lpCmdLine);

    std::wstring install_dir = GetInstallDir(cfg);
    std::wstring bootstrapper = fs::path(install_dir).wstring() + L"\\" + L"AisakaLauncher.exe";

    if (HasFlag(lpCmdLine, L"--uninstall")) {
        RunUninstall(install_dir, bootstrapper);
        HttpGlobalCleanup();
        return 0;
    }

    std::optional<PlayArgs> play_args = ReadJoinScript(lpCmdLine);

    auto& localeMgr = LocaleManager::getInstance();

    CMainDialog dialog(hInstance);
    dialog.InitDialog();
    dialog.ShowWindow();
    dialog.SetMessage(localeMgr.getLocalizedString("connecting"));
    dialog.SetMarquee(true);
    dialog.SetCancelVisible(false);

    std::atomic<bool> cancelled{ false };
    dialog.closeCallback = [&cancelled]() { cancelled.store(true); };

    fs::create_directories(install_dir);

    const PlayArgs* play_args_ptr = play_args ? &(*play_args) : nullptr;

    bool tray_mode = false;
    std::wstring last_message;

    auto status_cb = [&](BootstrapStatus s) {
        if (cancelled.load()) return;
        if (!s.error.empty()) {
            dialog.PostError(s.error);
            return;
        }
        if (s.tray_mode) tray_mode = true;
        if (s.close_dialog) {
            dialog.PostClose();
            return;
        }
        if (s.show_cancel)
            dialog.SetCancelVisible(true);
        if (!s.message.empty() && s.message != last_message) {
            last_message = s.message;
            dialog.SetMessage(s.message);
        }
        if (s.percent == -1) {
            dialog.SetMarquee(true);
        } else {
            dialog.SetMarquee(false);
            dialog.SetProgress(s.percent);
        }
        if (s.success)
            dialog.PostSuccess();
    };

    std::thread worker([&]() {
        if (CheckBootstrapperUpdate(install_dir, bootstrapper, lpCmdLine, status_cb, cancelled))
            return;
        RunBootstrap(cfg, cdn_url, base_url, install_dir, bootstrapper, play_args_ptr, status_cb, cancelled);
    });

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    cancelled.store(true);
    if (worker.joinable()) worker.join();

    if (tray_mode) {
        long long place_id = 0;
        if (play_args) {
            const auto& url = play_args->place_launcher_url;
            auto pos = url.find("placeId=");
            if (pos != std::string::npos) {
                try { place_id = std::stoll(url.substr(pos + 8)); } catch (...) {}
            }
        }
        RunTrayLoop(hInstance, place_id);
    }

    HttpGlobalCleanup();
    return static_cast<int>(msg.wParam);
}
