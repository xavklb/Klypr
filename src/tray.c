#include "tray.h"
#include "launcher.h"
#include "config.h"
#include "input.h"
#include "app.h"
#include <shellapi.h>
#include <stdio.h>

#define IDM_TRAY_OPEN      2001
#define IDM_TRAY_TERMINAL  2002
#define IDM_TRAY_CONFIG    2003
#define IDM_TRAY_AUTOSTART 2004
#define IDM_TRAY_QUIT      2005

static NOTIFYICONDATAW s_nid = {0};
static bool s_tray_created = false;
static HICON s_tray_icon = NULL;
static UINT s_uTaskbarRestartMsg = 0;

static HICON create_tray_icon(void)
{
    int cx = GetSystemMetrics(SM_CXSMICON);
    int cy = GetSystemMetrics(SM_CYSMICON);
    if (cx <= 0) cx = 16;
    if (cy <= 0) cy = 16;

    HDC hdcScreen = GetDC(NULL);
    HDC hdcColor = CreateCompatibleDC(hdcScreen);
    HDC hdcMask = CreateCompatibleDC(hdcScreen);

    HBITMAP hbmColor = CreateCompatibleBitmap(hdcScreen, cx, cy);
    HBITMAP hbmMask = CreateBitmap(cx, cy, 1, 1, NULL);

    HGDIOBJ oldColorBmp = SelectObject(hdcColor, hbmColor);
    HGDIOBJ oldMaskBmp = SelectObject(hdcMask, hbmMask);

    // 1. Fill mask with white (1 = transparent)
    HBRUSH whiteBrush = (HBRUSH)GetStockObject(WHITE_BRUSH);
    RECT fullRect = {0, 0, cx, cy};
    FillRect(hdcMask, &fullRect, whiteBrush);

    // 2. Draw black rounded rectangle on mask (0 = opaque)
    HBRUSH blackBrush = (HBRUSH)GetStockObject(BLACK_BRUSH);
    HPEN blackPen = (HPEN)GetStockObject(BLACK_PEN);
    SelectObject(hdcMask, blackBrush);
    SelectObject(hdcMask, blackPen);
    RoundRect(hdcMask, 0, 0, cx, cy, cx / 3, cy / 3);

    // 3. Fill color bitmap background with black
    FillRect(hdcColor, &fullRect, (HBRUSH)GetStockObject(BLACK_BRUSH));

    // 4. Draw dark rounded tile on color bitmap
    HBRUSH bgBrush = CreateSolidBrush(RGB(18, 20, 24));
    HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(0, 180, 255));
    SelectObject(hdcColor, bgBrush);
    SelectObject(hdcColor, borderPen);
    RoundRect(hdcColor, 0, 0, cx, cy, cx / 3, cy / 3);

    // 5. Draw cyan prompt chevron '>'
    HPEN arrowPen = CreatePen(PS_SOLID, cx >= 24 ? 2 : 1, RGB(0, 235, 255));
    SelectObject(hdcColor, arrowPen);
    int midY = cy / 2;
    int startX = cx / 4;
    int endX = cx / 2;
    MoveToEx(hdcColor, startX, midY - (cy / 4), NULL);
    LineTo(hdcColor, endX, midY);
    LineTo(hdcColor, startX, midY + (cy / 4) + 1);

    // 6. Draw white cursor line '_'
    HPEN cursorPen = CreatePen(PS_SOLID, cx >= 24 ? 2 : 1, RGB(255, 255, 255));
    SelectObject(hdcColor, cursorPen);
    MoveToEx(hdcColor, endX + 2, midY + (cy / 4), NULL);
    LineTo(hdcColor, cx - (cx / 5), midY + (cy / 4));

    SelectObject(hdcColor, oldColorBmp);
    SelectObject(hdcMask, oldMaskBmp);

    DeleteObject(cursorPen);
    DeleteObject(arrowPen);
    DeleteObject(borderPen);
    DeleteObject(bgBrush);
    DeleteDC(hdcColor);
    DeleteDC(hdcMask);
    ReleaseDC(NULL, hdcScreen);

    ICONINFO ii = {0};
    ii.fIcon = TRUE;
    ii.hbmMask = hbmMask;
    ii.hbmColor = hbmColor;

    HICON hIcon = CreateIconIndirect(&ii);

    DeleteObject(hbmColor);
    DeleteObject(hbmMask);

    if (hIcon == NULL) {
        hIcon = CopyIcon(LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION));
    }

    return hIcon;
}

bool tray_init(HWND hwnd_notify)
{
    if (hwnd_notify == NULL) {
        return false;
    }

    s_uTaskbarRestartMsg = RegisterWindowMessageW(L"TaskbarCreated");

    if (s_tray_icon == NULL) {
        s_tray_icon = create_tray_icon();
    }

    memset(&s_nid, 0, sizeof(NOTIFYICONDATAW));
    s_nid.cbSize = sizeof(NOTIFYICONDATAW);
    s_nid.hWnd = hwnd_notify;
    s_nid.uID = 1;
    s_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    s_nid.uCallbackMessage = WM_TRAY_NOTIFY;
    s_nid.hIcon = s_tray_icon;
    wcsncpy(s_nid.szTip, L"Klypr - Lanceur d'applications (Ctrl+A)", sizeof(s_nid.szTip) / sizeof(s_nid.szTip[0]) - 1);

    s_tray_created = Shell_NotifyIconW(NIM_ADD, &s_nid) != FALSE;
    return s_tray_created;
}

void tray_cleanup(void)
{
    if (s_tray_created) {
        Shell_NotifyIconW(NIM_DELETE, &s_nid);
        s_tray_created = false;
    }
    if (s_tray_icon != NULL) {
        DestroyIcon(s_tray_icon);
        s_tray_icon = NULL;
    }
}

void tray_refresh(void)
{
    if (s_nid.hWnd != NULL && s_tray_icon != NULL) {
        Shell_NotifyIconW(NIM_ADD, &s_nid);
        s_tray_created = true;
    }
}

UINT tray_get_taskbar_restart_msg(void)
{
    return s_uTaskbarRestartMsg;
}

void tray_handle_message(HWND hwnd, WPARAM wParam, LPARAM lParam)
{
    (void)wParam;

    if (lParam == WM_LBUTTONUP) {
        launcher_toggle();
        return;
    }

    if (lParam == WM_LBUTTONDBLCLK) {
        launcher_show();
        return;
    }

    if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
        POINT pt;
        GetCursorPos(&pt);
        SetForegroundWindow(hwnd);

        HMENU hMenu = CreatePopupMenu();
        if (hMenu == NULL) {
            return;
        }

        AppendMenuW(hMenu, MF_STRING, IDM_TRAY_OPEN, L"Ouvrir Klypr\t(Ctrl+A)");
        SetMenuDefaultItem(hMenu, IDM_TRAY_OPEN, FALSE);

        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(hMenu, MF_STRING, IDM_TRAY_TERMINAL, L"Ouvrir le terminal\t(Alt+Entr\u00e9e)");
        AppendMenuW(hMenu, MF_STRING, IDM_TRAY_CONFIG, L"\u00c9diter la configuration (klypr.ini)");

        UINT autostart_flag = MF_STRING | (g_config.autostart ? MF_CHECKED : MF_UNCHECKED);
        AppendMenuW(hMenu, autostart_flag, IDM_TRAY_AUTOSTART, L"Lancer au d\u00e9marrage de Windows");

        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(hMenu, MF_STRING, IDM_TRAY_QUIT, L"Quitter Klypr\t(Alt+Shift+Q)");

        int cmd = TrackPopupMenu(
            hMenu,
            TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
            pt.x,
            pt.y,
            0,
            hwnd,
            NULL
        );

        PostMessageW(hwnd, WM_NULL, 0, 0);
        DestroyMenu(hMenu);

        switch (cmd) {
        case IDM_TRAY_OPEN:
            launcher_show();
            break;
        case IDM_TRAY_TERMINAL:
            input_launch_terminal();
            break;
        case IDM_TRAY_CONFIG:
            ShellExecuteW(NULL, L"open", config_get_ini_path(), NULL, NULL, SW_SHOWNORMAL);
            break;
        case IDM_TRAY_AUTOSTART:
            config_set_autostart(!g_config.autostart);
            launcher_refresh();
            break;
        case IDM_TRAY_QUIT:
            app_stop();
            break;
        default:
            break;
        }
    }
}
