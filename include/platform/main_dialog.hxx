#pragma once

#include <windows.h>
#include <string>
#include <functional>

#define IDC_PROGRESS 1001
#define IDB_LOGO 200
#define IDI_BOOTSTRAPPER 100


class CMainDialog
{
public:
    CMainDialog(HINSTANCE hInstance);
    ~CMainDialog();

    void InitDialog();
    void ShowWindow();
    void CloseDialog();

    void ShowSuccess();
    bool isSuccessShown() const { return showSuccess; }

    void PostSuccess();
    void PostClose();
    void PostError(const std::wstring& message);

    void SetMessage(const std::wstring& message);
    void SetMarquee(bool state);
    void SetProgress(int percent);

    void SetCancelEnabled(bool state);
    void SetCancelVisible(bool state);

    int MessageBox(LPCWSTR text, LPCWSTR caption, UINT uType);

    bool GetSilentMode() const { return false; }

    std::function<void()> closeCallback;

protected:
    void OnCreate(HWND hWnd);
    HWND CreateTextHelper(HWND hWndParent, HFONT font, int x, int y, int width, int height, bool hidden);

private:
    HINSTANCE hInstance;
    bool dialogClosing;
    bool showSuccess;

    HWND hWndDialog;
    HWND hWndText;
    HWND hWndProgressBar;
    HWND hWndButtonCancel;
    HWND hWndButtonOk;
    HWND hInstallSuccess;

    HFONT fontMsg;
    HFONT fontTitle;
    HBITMAP logo;

    double progressTarget;
    double progressSmooth;
    bool isMarquee;
    double marqueePos;

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK ProgressWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    static VOID CALLBACK TimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);
};
