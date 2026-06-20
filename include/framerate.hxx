#pragma once
#include <windows.h>
#include <vector>
#include <string>

namespace Framerate {
    void SetCap(double cap);
    void SetExitTarget(HWND hwnd, UINT msg);
    void SetProcesses(const std::vector<std::wstring>& players, const std::vector<std::wstring>& studios);
    void SetTypeChangeTarget(HWND hwnd, UINT msg);
    HANDLE StartWatchThread();
    void StopWatchThread(HANDLE thread);
}
