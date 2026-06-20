#include "framerate.hxx"

#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <vector>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <mutex>
#include <limits>
#include <cstdint>
#include <cstring>

#pragma comment(lib, "psapi.lib")

namespace {

bool SigCompare(const char* loc, const char* aob, const char* mask) {
    for (; *mask; ++aob, ++mask, ++loc)
        if (*mask == 'x' && *loc != *aob) return false;
    return true;
}

uint8_t* SigScan(const char* aob, const char* mask, uintptr_t start, uintptr_t end) {
    size_t len = strlen(mask);
    for (; start + len <= end; ++start)
        if (SigCompare((const char*)start, aob, mask)) return (uint8_t*)start;
    return nullptr;
}

std::vector<HANDLE> GetProcessesByName(const wchar_t* name) {
    std::vector<HANDLE> result;
    PROCESSENTRY32W entry{ sizeof(entry) };
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return result;
    if (Process32FirstW(snap, &entry))
        do {
            if (_wcsicmp(entry.szExeFile, name) == 0)
                if (HANDLE h = OpenProcess(PROCESS_ALL_ACCESS, FALSE, entry.th32ProcessID))
                    result.push_back(h);
        } while (Process32NextW(snap, &entry));
    CloseHandle(snap);
    return result;
}

bool IsProcess64Bit(HANDLE process) {
#ifdef _WIN64
    BOOL wow64 = FALSE;
    IsWow64Process(process, &wow64);
    return !wow64;
#else
    return false;
#endif
}

bool GetMainModuleInfo(HANDLE process, void*& base_out, size_t& size_out) {
    char path[MAX_PATH]{};
    DWORD sz = MAX_PATH;
    if (!QueryFullProcessImageNameA(process, 0, path, &sz)) return false;

    DWORD needed = 0;
    EnumProcessModulesEx(process, nullptr, 0, &needed, LIST_MODULES_ALL);
    std::vector<HMODULE> mods(needed / sizeof(HMODULE));
    if (!EnumProcessModulesEx(process, mods.data(), needed, &needed, LIST_MODULES_ALL)) return false;

    for (auto mod : mods) {
        char mod_path[MAX_PATH]{};
        if (!GetModuleFileNameExA(process, mod, mod_path, MAX_PATH)) continue;
        if (_stricmp(mod_path, path) != 0) continue;
        MODULEINFO mi{};
        if (!GetModuleInformation(process, mod, &mi, sizeof(mi))) continue;
        base_out = mi.lpBaseOfDll;
        size_out = mi.SizeOfImage;
        return true;
    }
    return false;
}

constexpr DWORD kPageReadable = PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_READONLY | PAGE_READWRITE;
constexpr size_t kReadLimit = 1024 * 1024 * 2;

void* ScanProcessMemory(HANDLE process, const char* aob, const char* mask, const uint8_t* start, const uint8_t* end) {
    std::vector<uint8_t> buf(kReadLimit);
    size_t aob_len = strlen(mask);
    const uint8_t* i = start;
    while (i < end) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQueryEx(process, i, &mbi, sizeof(mbi))) return nullptr;
        size_t region = mbi.RegionSize - (i - (const uint8_t*)mbi.BaseAddress);
        if (i + region > end) region = end - i;
        if ((mbi.State & MEM_COMMIT) && (mbi.Protect & kPageReadable) && !(mbi.Protect & PAGE_GUARD)) {
            size_t offset = 0;
            while (offset + aob_len <= region) {
                size_t chunk = (region - offset < kReadLimit) ? (region - offset) : kReadLimit;
                SIZE_T bytes_read = 0;
                if (ReadProcessMemory(process, i + offset, buf.data(), chunk, &bytes_read) && bytes_read >= aob_len) {
                    if (uint8_t* hit = SigScan(aob, mask, (uintptr_t)buf.data(), (uintptr_t)buf.data() + bytes_read))
                        return (uint8_t*)(i + offset) + (hit - buf.data());
                }
                if (bytes_read > aob_len) bytes_read -= (SIZE_T)aob_len;
                if (bytes_read == 0) break;
                offset += bytes_read;
            }
        }
        i += region;
    }
    return nullptr;
}

template<typename T>
T ReadMem(HANDLE h, const void* addr) {
    T v{};
    ReadProcessMemory(h, addr, &v, sizeof(T), nullptr);
    return v;
}

bool ReadMemBuf(HANDLE h, const void* addr, void* buf, size_t n) {
    SIZE_T r = 0;
    return ReadProcessMemory(h, addr, buf, n, &r) && r == n;
}

template<typename T>
void WriteMem(HANDLE h, const void* addr, const T& val) {
    WriteProcessMemory(h, (LPVOID)addr, &val, sizeof(T), nullptr);
}

const void* ReadPointer(HANDLE h, const void* addr) {
#ifdef _WIN64
    if (IsProcess64Bit(h))
        return (const void*)ReadMem<uint64_t>(h, addr);
    return (const void*)ReadMem<uint32_t>(h, addr);
#else
    return (const void*)ReadMem<uint32_t>(h, addr);
#endif
}

const void* FindTaskScheduler(HANDLE process) {
    void* base = nullptr;
    size_t size = 0;
    for (int t = 0; t < 5; ++t) {
        if (GetMainModuleInfo(process, base, size) && base) break;
        Sleep(200);
    }
    if (!base) return nullptr;

    bool is64 = IsProcess64Bit(process);
    auto start = (const uint8_t*)base;
    auto end = start + size;

    if (is64) {
        auto result = (const uint8_t*)ScanProcessMemory(process, "\x40\x53\x48\x83\xEC\x20\x0F\xB6\xD9\xE8\x00\x00\x00\x00\x86\x58\x04\x48\x83\xC4\x20\x5B\xC3", "xxxxxxxxxx????xxxxxxxxx", start, end);
        if (!result) return nullptr;

        auto gts_fn = result + 14 + ReadMem<int32_t>(process, result + 10);

        uint8_t buf[0x100]{};
        if (!ReadMemBuf(process, gts_fn, buf, sizeof(buf))) return nullptr;

        auto inst = SigScan("\x48\x8B\x05\x00\x00\x00\x00\x48\x83\xC4\x38", "xxx????xxxx", (uintptr_t)buf, (uintptr_t)buf + sizeof(buf));
        if (!inst) return nullptr;

        const uint8_t* remote = gts_fn + (inst - buf);
        return remote + 7 + *(int32_t*)(inst + 3);
    } else {
        auto result = (const uint8_t*)ScanProcessMemory(process, "\x55\x8B\xEC\xE8\x00\x00\x00\x00\x8A\x4D\x08\x83\xC0\x04\x86\x08\x5D\xC3", "xxxx????xxxxxxxxxx", start, end);
        if (!result) return nullptr;

        auto gts_fn = result + 8 + ReadMem<int32_t>(process, result + 4);

        uint8_t buf[0x100]{};
        if (!ReadMemBuf(process, gts_fn, buf, sizeof(buf))) return nullptr;

        auto inst = SigScan("\xA1\x00\x00\x00\x00\x8B\x4D\xF4", "x????xxx", (uintptr_t)buf, (uintptr_t)buf + sizeof(buf));
        if (!inst) return nullptr;

        return (const void*)*(uint32_t*)(inst + 1);
    }
}

size_t FindFrameDelayOffset(HANDLE process, const void* scheduler) {
    constexpr size_t kSearchOff = 0x100;
    uint8_t buf[0x100]{};
    if (!ReadMemBuf(process, (const uint8_t*)scheduler + kSearchOff, buf, sizeof(buf)))
        return (size_t)-1;
    static const double kFrameDelay = 1.0 / 60.0;
    for (int i = 0; i < (int)(sizeof(buf) - sizeof(double)); i += 4) {
        double diff = *(double*)(buf + i) - kFrameDelay;
        if (diff < 0) diff = -diff;
        if (diff < std::numeric_limits<double>::epsilon())
            return kSearchOff + i;
    }
    return (size_t)-1;
}

struct GameProcess {
    HANDLE handle = nullptr;
    const void* ts_ptr = nullptr;
    const void* fd_ptr = nullptr;
    int retries = 3;
    std::wstring bin;

    void ApplyCap(double cap) {
        if (!fd_ptr) return;
        static const double kMinDelay = 1.0 / 10000.0;
        double delay = cap <= 0.0 ? kMinDelay : 1.0 / cap;
        WriteProcessMemory(handle, (LPVOID)fd_ptr, &delay, sizeof(delay), nullptr);
    }

    void Tick(double cap) {
        if (retries < 0) return;
        if (!ts_ptr) {
            ts_ptr = FindTaskScheduler(handle);
            if (!ts_ptr) { --retries; return; }
        }
        if (!fd_ptr) {
            auto sched = (const uint8_t*)ReadPointer(handle, ts_ptr);
            if (!sched) return;
            size_t off = FindFrameDelayOffset(handle, sched);
            if (off == (size_t)-1) { --retries; return; }
            fd_ptr = sched + off;
            ApplyCap(cap);
        }
    }
};

std::unordered_map<DWORD, GameProcess> g_processes;
double g_default_framerate = 60.0;

std::mutex g_processes_mutex;
std::vector<std::wstring> g_players;
std::vector<std::wstring> g_studios;

HWND g_exit_hwnd = nullptr;
UINT g_exit_msg = 0;
HWND g_type_hwnd = nullptr;
UINT g_type_msg = 0;
uint32_t g_prev_type_mask = 0;

DWORD WINAPI WatchThreadProc(LPVOID) {
    bool had_process = false;

    while (true) {
        std::vector<std::pair<HANDLE, std::wstring>> found;
        {
            std::lock_guard<std::mutex> lock(g_processes_mutex);
            for (const auto& name : g_players)
                for (auto h : GetProcessesByName(name.c_str()))
                    found.push_back({h, name});
            for (const auto& name : g_studios)
                for (auto h : GetProcessesByName(name.c_str()))
                    found.push_back({h, name});
        }

        for (auto& [h, name] : found) {
            DWORD id = GetProcessId(h);
            if (g_processes.find(id) == g_processes.end()) {
                had_process = true;
                GameProcess gp;
                gp.handle = h;
                gp.bin = name;
                gp.Tick(g_default_framerate);
                g_processes[id] = std::move(gp);
            } else {
                CloseHandle(h);
            }
        }

        for (auto it = g_processes.begin(); it != g_processes.end();) {
            DWORD code = STILL_ACTIVE;
            GetExitCodeProcess(it->second.handle, &code);
            if (code != STILL_ACTIVE) {
                CloseHandle(it->second.handle);
                it = g_processes.erase(it);
            } else {
                it->second.Tick(g_default_framerate);
                ++it;
            }
        }

        uint32_t new_mask = 0;
        {
            std::lock_guard<std::mutex> lock(g_processes_mutex);
            for (const auto& [pid, gp] : g_processes) {
                if (std::find(g_players.begin(), g_players.end(), gp.bin) != g_players.end())
                    new_mask |= 1;
                if (std::find(g_studios.begin(), g_studios.end(), gp.bin) != g_studios.end())
                    new_mask |= 2;
            }
        }
        if (new_mask != g_prev_type_mask && g_type_hwnd) {
            PostMessageW(g_type_hwnd, g_type_msg, new_mask, 0);
            g_prev_type_mask = new_mask;
        }

        if (had_process && g_processes.empty() && g_exit_hwnd)
            PostMessageW(g_exit_hwnd, g_exit_msg, 0, 0);
        Sleep(2000);
    }
    return 0;
}

}

namespace Framerate {

    void SetProcesses(const std::vector<std::wstring>& players, const std::vector<std::wstring>& studios) {
        std::lock_guard<std::mutex> lock(g_processes_mutex);
        g_players = players;
        g_studios = studios;
        g_prev_type_mask = 0;
    }

    void SetTypeChangeTarget(HWND hwnd, UINT msg) {
        g_type_hwnd = hwnd;
        g_type_msg = msg;
    }

    void SetCap(double cap) {
        g_default_framerate = cap;
        for (auto& [id, gp] : g_processes)
            gp.ApplyCap(cap);
    }

    void SetExitTarget(HWND hwnd, UINT msg) {
        g_exit_hwnd = hwnd;
        g_exit_msg = msg;
    }

    HANDLE StartWatchThread() {
        return CreateThread(nullptr, 0, WatchThreadProc, nullptr, 0, nullptr);
    }

    void StopWatchThread(HANDLE thread) {
        if (thread) {
            TerminateThread(thread, 0);
            CloseHandle(thread);
        }
        for (auto& [id, gp] : g_processes)
            CloseHandle(gp.handle);
        g_processes.clear();
    }
}