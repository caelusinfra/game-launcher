#include "platform/main_dialog.hxx"
#include "locales.hxx"

#include <commctrl.h>
#include <dwmapi.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "msimg32.lib")

#define IDT_TIMER 1
#define IDT_ANIM 2
const static int WM_MARQUEE = WM_USER + 101;
const static int WM_PROGRESS = WM_USER + 102;
const static int WM_SHOW_SUCCESS = WM_APP + 1;
const static int WM_SHOW_ERROR = WM_APP + 2;
const static int WM_CLOSE_DIALOG = WM_APP + 3;

static HICON icon;
static bool s_okHovered = false;
static bool s_cancelHovered = false;
WNDPROC OldCancelBtnProc;
WNDPROC OldOkBtnProc;

CMainDialog::CMainDialog(HINSTANCE hInstance)
    : hInstance(hInstance), dialogClosing(false), showSuccess(false),
      hWndDialog(NULL), hWndText(NULL), hWndProgressBar(NULL),
      hWndButtonCancel(NULL), hWndButtonOk(NULL), hInstallSuccess(NULL),
      fontMsg(NULL), fontTitle(NULL), logo(NULL),
      progressTarget(0.0), progressSmooth(0.0), isMarquee(true), marqueePos(0.0)
{
    icon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_BOOTSTRAPPER));
    logo = LoadBitmap(GetModuleHandle(NULL), MAKEINTRESOURCE(IDB_LOGO));
}

CMainDialog::~CMainDialog()
{
    CloseDialog();
}

HWND CMainDialog::CreateTextHelper(HWND hWndParent, HFONT font, int x, int y, int width, int height, bool hidden)
{
    DWORD style = WS_CHILD | SS_CENTER;
    if (!hidden) style |= WS_VISIBLE;

    HWND hWndText = CreateWindowExW(0, L"Static", L"", style, x, y, width, height, hWndParent, NULL, GetModuleHandle(NULL), NULL);

    SendMessage(hWndText, WM_SETFONT, (WPARAM)font, TRUE);
    return hWndText;
}

LRESULT CALLBACK CMainDialog::WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    static HBRUSH hbrBackground = CreateSolidBrush(RGB(255, 255, 255));
    static CMainDialog* dialog = NULL;

    switch (uMsg)
    {
    case WM_CREATE:
        {
            CREATESTRUCTW *pCreate = reinterpret_cast<CREATESTRUCTW*>(lParam);
            dialog = static_cast<CMainDialog*>(pCreate->lpCreateParams);
            dialog->OnCreate(hwnd);

            SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)icon);
            SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)icon);
        }
        break;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDCANCEL)
        {
            SendMessage(hwnd, WM_CLOSE, 0, 0);
        }
        else if (LOWORD(wParam) == IDOK)
        {
            SendMessage(hwnd, WM_CLOSE, 0, 0);
        }
        break;
    case WM_NCCALCSIZE:
        return 0;
    case WM_CTLCOLORSTATIC:
        {
            HDC hdcStatic = (HDC)wParam;
            SetTextColor(hdcStatic, RGB(25, 25, 25));
            SetBkMode(hdcStatic, TRANSPARENT);
            return (LRESULT)hbrBackground;
        }
        break;
    case WM_DESTROY:
        if (dialog && !dialog->dialogClosing && dialog->closeCallback)
            dialog->closeCallback();
        PostQuitMessage(0);
        return 0;
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            FillRect(hdc, &ps.rcPaint, hbrBackground);

            HPEN hPen = CreatePen(PS_SOLID, 1, RGB(184, 184, 184));
            HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
            
            RECT rect;
            GetWindowRect(hwnd, &rect);
            int width = rect.right - rect.left;
            int height = rect.bottom - rect.top;

            
            Rectangle(hdc, 0, 0, width, height);
            SelectObject(hdc, hOldPen);
            DeleteObject(hPen);

            EndPaint(hwnd, &ps);
        }
        return 0;
    case WM_ACTIVATE:
        InvalidateRect(hwnd, 0, NULL);
        break;
    case WM_PROGRESS:
        if (dialog)
            dialog->progressTarget = static_cast<double>(wParam);
        break;
    case WM_MARQUEE:
        if (dialog) {
            bool entering_progress = (wParam == 0) && dialog->isMarquee;
            dialog->isMarquee = (wParam != 0);
            if (entering_progress)
                dialog->progressSmooth = 0.0;
            if (dialog->hWndProgressBar)
                InvalidateRect(dialog->hWndProgressBar, nullptr, FALSE);
        }
        break;
    case WM_TIMER:
        if (wParam == IDT_ANIM && dialog && dialog->hWndProgressBar) {
            if (dialog->isMarquee) {
                dialog->marqueePos += 0.007;
                if (dialog->marqueePos > 1.0) dialog->marqueePos -= 1.0;
            } else {
                double diff = dialog->progressTarget - dialog->progressSmooth;
                if (diff < 0.05 && diff > -0.05)
                    dialog->progressSmooth = dialog->progressTarget;
                else
                    dialog->progressSmooth += diff * 0.12;
            }
            InvalidateRect(dialog->hWndProgressBar, nullptr, FALSE);
        }
        break;
    case WM_SHOW_SUCCESS:
        if (dialog) dialog->ShowSuccess();
        return 0;
    case WM_CLOSE_DIALOG:
        if (dialog) dialog->dialogClosing = true;
        DestroyWindow(hwnd);
        return 0;
    case WM_SHOW_ERROR:
        {
            auto* msg = reinterpret_cast<std::wstring*>(lParam);
            if (msg) {
                ::MessageBoxW(hwnd, msg->c_str(), L"Error", MB_OK | MB_ICONERROR);
                delete msg;
            }
            PostQuitMessage(1);
        }
        return 0;
    case WM_DRAWITEM:
    {
        LPDRAWITEMSTRUCT pDIS = (LPDRAWITEMSTRUCT)lParam;

        if (pDIS->CtlID == IDCANCEL)
        {
            HDC hdc = pDIS->hDC;
            RECT rect = pDIS->rcItem;

            bool isPressed = (pDIS->itemState & ODS_SELECTED);
            COLORREF bgColor = isPressed ? RGB(245, 245, 245) : s_cancelHovered ? RGB(248, 248, 248) : RGB(255, 255, 255);
            COLORREF borderColor = isPressed ? RGB(150, 150, 150) : s_cancelHovered ? RGB(160, 160, 160) : RGB(184, 184, 184);
            COLORREF textColor = RGB(117, 117, 117);

            HPEN hPen = CreatePen(PS_SOLID, 1, borderColor);
            HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);

            HBRUSH hBrush = CreateSolidBrush(bgColor);
            HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hBrush);

            RoundRect(hdc, rect.left, rect.top, rect.right, rect.bottom, 8, 8);

            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, textColor);

            wchar_t szText[256];
            GetWindowTextW(pDIS->hwndItem, szText, 256);

            HFONT hFont = CreateFontW(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
            HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

            DrawTextW(hdc, szText, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            SelectObject(hdc, hOldFont);
            DeleteObject(hFont);
            SelectObject(hdc, hOldBrush);
            DeleteObject(hBrush);
            SelectObject(hdc, hOldPen);
            DeleteObject(hPen);

            return TRUE;
        } else if (pDIS->CtlID == IDOK) {
            HDC hdc = pDIS->hDC;
            RECT rect = pDIS->rcItem;

            bool isPressed = (pDIS->itemState & ODS_SELECTED);
            COLORREF bgColor = isPressed ? RGB(220, 70, 70) : s_okHovered ? RGB(255, 117, 117) : RGB(255, 94, 94);
            COLORREF borderColor = bgColor;
            COLORREF textColor = RGB(255, 255, 255);

            HPEN hPen = CreatePen(PS_SOLID, 1, borderColor);
            HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);

            HBRUSH hBrush = CreateSolidBrush(bgColor);
            HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hBrush);

            RoundRect(hdc, rect.left, rect.top, rect.right, rect.bottom, 8, 8);

            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, textColor);

            wchar_t szText[256];
            GetWindowTextW(pDIS->hwndItem, szText, 256);

            HFONT hFont = CreateFontW(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
            HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

            DrawTextW(hdc, szText, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            SelectObject(hdc, hOldFont);
            DeleteObject(hFont);
            SelectObject(hdc, hOldBrush);
            DeleteObject(hBrush);
            SelectObject(hdc, hOldPen);
            DeleteObject(hPen);

            return TRUE;
        }
        break;
    }
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

static LRESULT CALLBACK OkButtonProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
    case WM_MOUSEMOVE:
        if (!s_okHovered) {
            s_okHovered = true;
            InvalidateRect(hwnd, nullptr, FALSE);
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
        }
        break;
    case WM_MOUSELEAVE:
        s_okHovered = false;
        InvalidateRect(hwnd, nullptr, FALSE);
        break;
    }
    return CallWindowProcW(OldOkBtnProc, hwnd, uMsg, wParam, lParam);
}

static LRESULT CALLBACK CancelButtonProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
    case WM_MOUSEMOVE:
        if (!s_cancelHovered) {
            s_cancelHovered = true;
            InvalidateRect(hwnd, nullptr, FALSE);
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
        }
        break;
    case WM_MOUSELEAVE:
        s_cancelHovered = false;
        InvalidateRect(hwnd, nullptr, FALSE);
        break;
    }
    return CallWindowProcW(OldCancelBtnProc, hwnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK CMainDialog::ProgressWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CMainDialog* dlg = reinterpret_cast<CMainDialog*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

    switch (uMsg) {
    case WM_ERASEBKGND:
        return TRUE;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rc;
        GetClientRect(hwnd, &rc);
        int w = rc.right;
        int h = rc.bottom;

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(hdc, w, h);
        HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

        HBRUSH trackBrush = CreateSolidBrush(RGB(225, 225, 225));
        FillRect(memDC, &rc, trackBrush);
        DeleteObject(trackBrush);

        HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(180, 180, 180));
        HPEN oldPen = (HPEN)SelectObject(memDC, borderPen);
        HBRUSH oldBrush = (HBRUSH)SelectObject(memDC, GetStockObject(NULL_BRUSH));
        Rectangle(memDC, 0, 0, w, h);
        SelectObject(memDC, oldPen);
        SelectObject(memDC, oldBrush);
        DeleteObject(borderPen);

        int x0 = 1, y0 = 1, x1 = w - 1, y1 = h - 1;
        int inner_w = x1 - x0;

        if (dlg && inner_w > 0) {
            auto drawGradient = [&](int gx0, int gx1) {
                if (gx1 <= gx0) return;
                TRIVERTEX vt[2] = {
                    { (LONG)gx0, (LONG)y0, 0xEE00, 0x5900, 0x5900, 0 },
                    { (LONG)gx1, (LONG)y1, 0xFF00, 0x5E00, 0x5E00, 0 }
                };
                GRADIENT_RECT gr = { 0, 1 };
                GradientFill(memDC, vt, 2, &gr, 1, GRADIENT_FILL_RECT_H);
            };

            if (dlg->isMarquee) {
                int block_w = inner_w * 30 / 100;
                int travel = inner_w + block_w;
                int bx = (int)(dlg->marqueePos * travel) - block_w + x0;
                drawGradient(bx < x0 ? x0 : bx, (bx + block_w) > x1 ? x1 : (bx + block_w));
            } else {
                int fill_w = (int)(dlg->progressSmooth * inner_w / 100.0);
                drawGradient(x0, x0 + (fill_w < inner_w ? fill_w : inner_w));
            }
        }

        BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBmp);
        DeleteObject(memBmp);
        DeleteDC(memDC);

        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

void CMainDialog::InitDialog()
{
    INITCOMMONCONTROLSEX c;
    c.dwSize = sizeof(INITCOMMONCONTROLSEX);
    c.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&c);

    WNDCLASSW wcp = {};
    wcp.lpfnWndProc = ProgressWndProc;
    wcp.hInstance = hInstance;
    wcp.lpszClassName = L"ProgressBar";
    wcp.style = CS_HREDRAW | CS_VREDRAW;
    RegisterClassW(&wcp);

    const wchar_t CLASS_NAME[] = L"MAINDIALOG";
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = CreateSolidBrush(RGB(255, 255, 255));
    wc.style = CS_HREDRAW | CS_VREDRAW;

    RegisterClassW(&wc);

    int sizeX = 500;
    int sizeY = 320;
    int posX = (GetSystemMetrics(SM_CXSCREEN) - sizeX) / 2;
    int posY = (GetSystemMetrics(SM_CYSCREEN) - sizeY) / 2;

    hWndDialog = CreateWindowExW(0, CLASS_NAME, L"", WS_POPUP | WS_THICKFRAME, posX, posY, sizeX, sizeY, NULL, NULL, hInstance, (LPVOID)this);
}

void CMainDialog::OnCreate(HWND hWnd)
{
    auto& localeMgr = LocaleManager::getInstance();
    RECT rect;
    GetWindowRect(hWnd, &rect);
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;

    MARGINS borderless = {1,1,1,1};
    DwmExtendFrameIntoClientArea(hWnd, &borderless);

    int logoSize = 110;
    HWND hPic = CreateWindowExW(0, WC_STATIC, L"", WS_CHILD | WS_VISIBLE | SS_BITMAP, width/2-logoSize/2, 67, logoSize, logoSize, hWnd, NULL, GetModuleHandle(NULL), NULL);
    SendMessage(hPic, STM_SETIMAGE, (WPARAM)IMAGE_BITMAP, (LPARAM)logo);

    HDC hdc = GetDC(NULL);
    long msgHeight = -MulDiv(11, GetDeviceCaps(hdc, LOGPIXELSY), 72);
    long titleHeight = -MulDiv(14, GetDeviceCaps(hdc, LOGPIXELSY), 72);
    fontMsg = CreateFontW(msgHeight,0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    fontTitle = CreateFontW(titleHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    hWndText = CreateTextHelper(hWnd, fontMsg, 5, 200, width - 10, 20, false);

    hInstallSuccess = CreateWindowExW(0, L"Static", localeMgr.getLocalizedString("install_success").c_str(), WS_CHILD | SS_CENTER | SS_CENTERIMAGE | SS_NOPREFIX, 5, 195, width - 10, 40, hWnd, NULL, GetModuleHandle(NULL), NULL);
    SendMessage(hInstallSuccess, WM_SETFONT, (WPARAM)fontTitle, TRUE);
    ::ShowWindow(hInstallSuccess, SW_HIDE);

    hWndProgressBar = CreateWindowExW(0, L"ProgressBar", nullptr, WS_CHILD | WS_VISIBLE, 24, 242, 452, 20, hWnd, (HMENU)IDC_PROGRESS, GetModuleHandle(NULL), nullptr);
    SetWindowLongPtr(hWndProgressBar, GWLP_USERDATA, (LONG_PTR)this);
    SetTimer(hWnd, IDT_ANIM, 16, nullptr);

    int bntWidth = 120;
    int bntHeight = 34;

    hWndButtonCancel = CreateWindowExW(0, L"Button", localeMgr.getLocalizedString("button_cancel").c_str(), WS_CHILD | BS_OWNERDRAW, width / 2 - bntWidth / 2, height - 50, bntWidth, bntHeight, hWnd, (HMENU)IDCANCEL, GetModuleHandle(NULL), NULL);
    hWndButtonOk = CreateWindowExW(0, L"Button", localeMgr.getLocalizedString("button_ok").c_str(), WS_CHILD | BS_OWNERDRAW, width/2-bntWidth/2, height - 50, bntWidth, bntHeight, hWnd, (HMENU)IDOK, GetModuleHandle(NULL), NULL);
    ::ShowWindow(hWndButtonOk, SW_HIDE);

    ReleaseDC(hWnd, hdc);

    SetWindowLongPtr(hWndButtonCancel, GWLP_USERDATA, (LONG_PTR)this);
    SetWindowLongPtr(hWndButtonOk, GWLP_USERDATA, (LONG_PTR)this);
    OldOkBtnProc = (WNDPROC)SetWindowLongPtrW(hWndButtonOk, GWLP_WNDPROC, (LONG_PTR)OkButtonProc);
    OldCancelBtnProc = (WNDPROC)SetWindowLongPtrW(hWndButtonCancel, GWLP_WNDPROC, (LONG_PTR)CancelButtonProc);
}

VOID CALLBACK CMainDialog::TimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
{}

void CMainDialog::ShowSuccess()
{
    showSuccess = true;

    ::ShowWindow(hWndButtonCancel, SW_HIDE);
    ::ShowWindow(hWndText, SW_HIDE);
    ::ShowWindow(hWndProgressBar, SW_HIDE);

    ::ShowWindow(hInstallSuccess, SW_SHOWNORMAL);
    ::ShowWindow(hWndButtonOk, SW_SHOWNORMAL);
    InvalidateRect(hWndDialog, 0, NULL);

    SetTimer(hWndDialog, IDT_TIMER, 300, (TIMERPROC)TimerProc);
}

int CMainDialog::MessageBox(LPCWSTR text, LPCWSTR caption, UINT uType) 
{
    if (GetSilentMode()) return 0;
    return ::MessageBoxW(hWndDialog, text, caption, uType);
}

void CMainDialog::ShowWindow()
{
    if (GetSilentMode()) return;
    if (hWndDialog) ::ShowWindow(hWndDialog, SW_SHOWNORMAL);
}

void CMainDialog::CloseDialog()
{
    dialogClosing = true;
    KillTimer(hWndDialog, IDT_TIMER);
    KillTimer(hWndDialog, IDT_ANIM);

    DeleteObject(logo);
    DeleteObject(fontMsg);
    DeleteObject(fontTitle);

    if (hWndDialog) {
        DestroyWindow(hWndDialog);
        hWndDialog = NULL;
    }
}

void CMainDialog::SetMessage(const std::wstring& message)
{
    if (GetSilentMode()) return;
    SendMessageW(hWndText, WM_SETTEXT, 0, (LPARAM)message.c_str());
}

void CMainDialog::SetCancelEnabled(bool state)
{
    if (GetSilentMode() || isSuccessShown()) return;
    EnableWindow(hWndButtonCancel, state ? TRUE : FALSE);
    ::ShowWindow(hWndButtonCancel, state ? SW_SHOWNORMAL : SW_HIDE);
}

void CMainDialog::SetCancelVisible(bool state)
{
    if (GetSilentMode() || isSuccessShown()) return;
    ::ShowWindow(hWndButtonCancel, state ? SW_SHOWNORMAL : SW_HIDE);
}

void CMainDialog::SetMarquee(bool state)
{
    if (GetSilentMode()) return;
    SendMessage(hWndDialog, WM_MARQUEE, state ? TRUE : FALSE, 0);
}

void CMainDialog::SetProgress(int percent)
{
    if (GetSilentMode()) return;
    SendMessage(hWndDialog, WM_PROGRESS, percent, 0);
}

void CMainDialog::PostSuccess()
{
    if (hWndDialog) PostMessage(hWndDialog, WM_SHOW_SUCCESS, 0, 0);
}

void CMainDialog::PostClose()
{
    if (hWndDialog) PostMessage(hWndDialog, WM_CLOSE_DIALOG, 0, 0);
}

void CMainDialog::PostError(const std::wstring& message)
{
    if (hWndDialog)
        PostMessage(hWndDialog, WM_SHOW_ERROR, 0, reinterpret_cast<LPARAM>(new std::wstring(message)));
}