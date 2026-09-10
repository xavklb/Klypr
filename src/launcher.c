#include "launcher.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <shellapi.h>

#define LAUNCHER_WIDTH 580
#define LAUNCHER_HEIGHT 54
#define LAUNCHER_CLASS_NAME L"KlyprLauncherClass"

static HWND s_hwnd_launcher = NULL;
static HWND s_hwnd_edit = NULL;
static WNDPROC s_old_edit_proc = NULL;
static HFONT s_font = NULL;
static HBRUSH s_bg_brush = NULL;
static HPEN s_border_pen = NULL;
static bool s_is_visible = false;

static const COLORREF COLOR_BG = RGB(22, 24, 29);
static const COLORREF COLOR_ACCENT = RGB(0, 230, 118);
static const COLORREF COLOR_TEXT = RGB(240, 244, 248);

static void execute_command(const wchar_t *input)
{
    if (input == NULL) {
        return;
    }

    while (*input == L' ' || *input == L'\t') {
        input++;
    }

    if (*input == L'\0') {
        return;
    }

    wchar_t cmd[MAX_PATH];
    const wchar_t *params = NULL;

    if (input[0] == L'"') {
        const wchar_t *end_quote = wcschr(input + 1, L'"');
        if (end_quote != NULL) {
            size_t len = (size_t)(end_quote - (input + 1));
            if (len >= MAX_PATH) {
                len = MAX_PATH - 1;
            }
            wcsncpy(cmd, input + 1, len);
            cmd[len] = L'\0';
            params = end_quote + 1;
            while (*params == L' ' || *params == L'\t') {
                params++;
            }
            if (*params == L'\0') {
                params = NULL;
            }
        } else {
            wcsncpy(cmd, input + 1, MAX_PATH - 1);
            cmd[MAX_PATH - 1] = L'\0';
        }
    } else {
        const wchar_t *space = wcschr(input, L' ');
        if (space != NULL) {
            size_t len = (size_t)(space - input);
            if (len >= MAX_PATH) {
                len = MAX_PATH - 1;
            }
            wcsncpy(cmd, input, len);
            cmd[len] = L'\0';
            params = space + 1;
            while (*params == L' ' || *params == L'\t') {
                params++;
            }
            if (*params == L'\0') {
                params = NULL;
            }
        } else {
            wcsncpy(cmd, input, MAX_PATH - 1);
            cmd[MAX_PATH - 1] = L'\0';
        }
    }

    // 1. Direct ShellExecute
    HINSTANCE hInst = ShellExecuteW(NULL, L"open", cmd, params, NULL, SW_SHOWNORMAL);
    if ((INT_PTR)hInst > 32) {
        return;
    }

    // 2. Try with .exe extension if not already present
    if (wcsstr(cmd, L".") == NULL) {
        wchar_t cmd_exe[MAX_PATH + 5];
        _snwprintf(cmd_exe, sizeof(cmd_exe) / sizeof(cmd_exe[0]), L"%s.exe", cmd);
        hInst = ShellExecuteW(NULL, L"open", cmd_exe, params, NULL, SW_SHOWNORMAL);
        if ((INT_PTR)hInst > 32) {
            return;
        }
    }

    // 3. Fallback: launch through cmd.exe start
    wchar_t cmd_args[1024];
    _snwprintf(cmd_args, sizeof(cmd_args) / sizeof(cmd_args[0]), L"/c start \"\" %s", input);
    ShellExecuteW(NULL, L"open", L"cmd.exe", cmd_args, NULL, SW_HIDE);
}

static LRESULT CALLBACK edit_subclass_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_KEYDOWN) {
        if (wParam == VK_RETURN) {
            wchar_t buffer[512] = {0};
            GetWindowTextW(hwnd, buffer, (int)(sizeof(buffer) / sizeof(buffer[0])));
            launcher_hide();
            execute_command(buffer);
            return 0;
        }

        if (wParam == VK_ESCAPE) {
            launcher_hide();
            return 0;
        }

        if ((wParam == 'A' || wParam == 'a') && (GetKeyState(VK_CONTROL) & 0x8000)) {
            SendMessageW(hwnd, EM_SETSEL, 0, -1);
            return 0;
        }
    }

    return CallWindowProcW(s_old_edit_proc, hwnd, uMsg, wParam, lParam);
}

static LRESULT CALLBACK launcher_wnd_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rect;
        GetClientRect(hwnd, &rect);

        // Fill background
        FillRect(hdc, &rect, s_bg_brush);

        // Draw border
        HGDIOBJ old_pen = SelectObject(hdc, s_border_pen);
        HGDIOBJ old_brush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
        Rectangle(hdc, rect.left, rect.top, rect.right, rect.bottom);
        Rectangle(hdc, rect.left + 1, rect.top + 1, rect.right - 1, rect.bottom - 1);
        SelectObject(hdc, old_brush);
        SelectObject(hdc, old_pen);

        // Draw terminal prompt ">"
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, COLOR_ACCENT);
        HGDIOBJ old_font = SelectObject(hdc, s_font);
        TextOutW(hdc, 16, 14, L">", 1);
        SelectObject(hdc, old_font);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, COLOR_TEXT);
        SetBkColor(hdc, COLOR_BG);
        return (LRESULT)s_bg_brush;
    }

    case WM_ACTIVATE: {
        if (LOWORD(wParam) == WA_INACTIVE) {
            launcher_hide();
        }
        return 0;
    }

    case WM_DESTROY: {
        launcher_cleanup();
        return 0;
    }

    default:
        break;
    }

    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

bool launcher_init(HINSTANCE hInstance)
{
    if (hInstance == NULL) {
        hInstance = GetModuleHandleW(NULL);
    }

    s_bg_brush = CreateSolidBrush(COLOR_BG);
    s_border_pen = CreatePen(PS_SOLID, 1, COLOR_ACCENT);

    s_font = CreateFontW(
        22,                        // cHeight
        0,                         // cWidth
        0,                         // cEscapement
        0,                         // cOrientation
        FW_SEMIBOLD,               // cWeight
        FALSE,                     // bItalic
        FALSE,                     // bUnderline
        FALSE,                     // bStrikeOut
        DEFAULT_CHARSET,           // iCharSet
        OUT_OUTLINE_PRECIS,        // iOutPrecision
        CLIP_DEFAULT_PRECIS,       // iClipPrecision
        CLEARTYPE_QUALITY,         // iQuality
        FIXED_PITCH | FF_MODERN,   // iPitchAndFamily
        L"Consolas"                // pszFaceName
    );

    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = launcher_wnd_proc;
    wc.hInstance = hInstance;
    wc.lpszClassName = LAUNCHER_CLASS_NAME;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = s_bg_brush;

    if (!RegisterClassExW(&wc)) {
        return false;
    }

    int screen_w = GetSystemMetrics(SM_CXSCREEN);
    int screen_h = GetSystemMetrics(SM_CYSCREEN);
    int pos_x = (screen_w - LAUNCHER_WIDTH) / 2;
    int pos_y = screen_h / 4;

    s_hwnd_launcher = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        LAUNCHER_CLASS_NAME,
        L"",
        WS_POPUP,
        pos_x,
        pos_y,
        LAUNCHER_WIDTH,
        LAUNCHER_HEIGHT,
        NULL,
        NULL,
        hInstance,
        NULL
    );

    if (s_hwnd_launcher == NULL) {
        return false;
    }

    s_hwnd_edit = CreateWindowExW(
        0,
        L"EDIT",
        L"",
        WS_CHILD | WS_VISIBLE | ES_LEFT | ES_AUTOHSCROLL,
        42,
        14,
        LAUNCHER_WIDTH - 58,
        26,
        s_hwnd_launcher,
        NULL,
        hInstance,
        NULL
    );

    if (s_hwnd_edit == NULL) {
        DestroyWindow(s_hwnd_launcher);
        s_hwnd_launcher = NULL;
        return false;
    }

    SendMessageW(s_hwnd_edit, WM_SETFONT, (WPARAM)s_font, TRUE);

    s_old_edit_proc = (WNDPROC)SetWindowLongPtrW(
        s_hwnd_edit,
        GWLP_WNDPROC,
        (LONG_PTR)edit_subclass_proc
    );

    s_is_visible = false;
    return true;
}

void launcher_cleanup(void)
{
    if (s_hwnd_launcher != NULL) {
        DestroyWindow(s_hwnd_launcher);
        s_hwnd_launcher = NULL;
        s_hwnd_edit = NULL;
    }

    if (s_font != NULL) {
        DeleteObject(s_font);
        s_font = NULL;
    }

    if (s_bg_brush != NULL) {
        DeleteObject(s_bg_brush);
        s_bg_brush = NULL;
    }

    if (s_border_pen != NULL) {
        DeleteObject(s_border_pen);
        s_border_pen = NULL;
    }

    UnregisterClassW(LAUNCHER_CLASS_NAME, GetModuleHandleW(NULL));
    s_is_visible = false;
}

void launcher_show(void)
{
    if (s_hwnd_launcher == NULL) {
        return;
    }

    // Center on screen dynamically
    int screen_w = GetSystemMetrics(SM_CXSCREEN);
    int screen_h = GetSystemMetrics(SM_CYSCREEN);
    int pos_x = (screen_w - LAUNCHER_WIDTH) / 2;
    int pos_y = screen_h / 4;

    SetWindowPos(
        s_hwnd_launcher,
        HWND_TOPMOST,
        pos_x,
        pos_y,
        LAUNCHER_WIDTH,
        LAUNCHER_HEIGHT,
        SWP_SHOWWINDOW
    );

    SetWindowTextW(s_hwnd_edit, L"");
    ShowWindow(s_hwnd_launcher, SW_SHOW);
    SetForegroundWindow(s_hwnd_launcher);
    SetFocus(s_hwnd_edit);
    s_is_visible = true;
}

void launcher_hide(void)
{
    if (s_hwnd_launcher == NULL) {
        return;
    }

    ShowWindow(s_hwnd_launcher, SW_HIDE);
    SetWindowTextW(s_hwnd_edit, L"");
    s_is_visible = false;
}

void launcher_toggle(void)
{
    if (s_is_visible) {
        launcher_hide();
    } else {
        launcher_show();
    }
}

bool launcher_is_visible(void)
{
    return s_is_visible && IsWindowVisible(s_hwnd_launcher);
}

HWND launcher_get_hwnd(void)
{
    return s_hwnd_launcher;
}
