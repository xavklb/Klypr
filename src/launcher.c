#include "launcher.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <shellapi.h>

#define LAUNCHER_WIDTH 580
#define BASE_HEIGHT 54
#define ITEM_HEIGHT 38
#define MAX_MATCHES 6
#define MAX_INDEXED_APPS 1024
#define LAUNCHER_CLASS_NAME L"KlyprLauncherClass"

typedef struct {
    wchar_t name[128];
    wchar_t target[MAX_PATH];
} AppEntry;

typedef struct {
    int app_index;
    int score;
} MatchEntry;

static HWND s_hwnd_launcher = NULL;
static HWND s_hwnd_edit = NULL;
static WNDPROC s_old_edit_proc = NULL;
static HFONT s_font = NULL;
static HBRUSH s_bg_brush = NULL;
static HBRUSH s_accent_brush = NULL;
static HPEN s_border_pen = NULL;
static bool s_is_visible = false;

static const COLORREF COLOR_BG = RGB(22, 24, 29);
static const COLORREF COLOR_ACCENT = RGB(0, 230, 118);
static const COLORREF COLOR_TEXT = RGB(240, 244, 248);

static AppEntry s_apps[MAX_INDEXED_APPS];
static int s_app_count = 0;

static MatchEntry s_matches[MAX_MATCHES];
static int s_match_count = 0;
static int s_selected_index = 0;

static bool wcs_contains_ci(const wchar_t *haystack, const wchar_t *needle)
{
    if (haystack == NULL || needle == NULL) {
        return false;
    }
    size_t nlen = wcslen(needle);
    size_t hlen = wcslen(haystack);
    if (nlen == 0 || nlen > hlen) {
        return false;
    }

    for (size_t i = 0; i <= hlen - nlen; i++) {
        bool match = true;
        for (size_t j = 0; j < nlen; j++) {
            if (towlower(haystack[i + j]) != towlower(needle[j])) {
                match = false;
                break;
            }
        }
        if (match) {
            return true;
        }
    }
    return false;
}

static bool wcs_starts_with_ci(const wchar_t *str, const wchar_t *prefix)
{
    if (str == NULL || prefix == NULL) {
        return false;
    }
    size_t plen = wcslen(prefix);
    if (wcslen(str) < plen) {
        return false;
    }
    for (size_t i = 0; i < plen; i++) {
        if (towlower(str[i]) != towlower(prefix[i])) {
            return false;
        }
    }
    return true;
}

static void add_app_entry(const wchar_t *name, const wchar_t *target)
{
    if (name == NULL || target == NULL || name[0] == L'\0' || s_app_count >= MAX_INDEXED_APPS) {
        return;
    }

    // Filter out common unwanted shortcuts
    if (wcs_contains_ci(name, L"uninstall") ||
        wcs_contains_ci(name, L"d\u00e9sinstall") ||
        wcs_contains_ci(name, L"desinstall") ||
        wcs_contains_ci(name, L"readme") ||
        wcs_contains_ci(name, L"aide") ||
        wcs_contains_ci(name, L"help") ||
        wcs_contains_ci(name, L"website")) {
        return;
    }

    // Avoid duplicates
    for (int i = 0; i < s_app_count; i++) {
        if (_wcsicmp(s_apps[i].name, name) == 0) {
            return;
        }
    }

    wcsncpy(s_apps[s_app_count].name, name, 127);
    s_apps[s_app_count].name[127] = L'\0';
    wcsncpy(s_apps[s_app_count].target, target, MAX_PATH - 1);
    s_apps[s_app_count].target[MAX_PATH - 1] = L'\0';
    s_app_count++;
}

static void scan_directory_for_shortcuts(const wchar_t *dir_path)
{
    wchar_t search_path[MAX_PATH];
    _snwprintf(search_path, MAX_PATH, L"%s\\*", dir_path);

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(search_path, &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        return;
    }

    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) {
            continue;
        }

        wchar_t full_path[MAX_PATH];
        _snwprintf(full_path, MAX_PATH, L"%s\\%s", dir_path, fd.cFileName);

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            scan_directory_for_shortcuts(full_path);
        } else {
            size_t len = wcslen(fd.cFileName);
            if (len > 4 && _wcsicmp(fd.cFileName + len - 4, L".lnk") == 0) {
                wchar_t base_name[128];
                size_t name_len = len - 4;
                if (name_len >= 128) {
                    name_len = 127;
                }
                wcsncpy(base_name, fd.cFileName, name_len);
                base_name[name_len] = L'\0';
                add_app_entry(base_name, full_path);
            }
        }
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
}

static void scan_app_paths_registry(void)
{
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD index = 0;
        wchar_t subkey_name[MAX_PATH];
        DWORD subkey_len = MAX_PATH;

        while (RegEnumKeyExW(hKey, index, subkey_name, &subkey_len, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
            wchar_t clean_name[128];
            wcsncpy(clean_name, subkey_name, 127);
            clean_name[127] = L'\0';
            size_t len = wcslen(clean_name);
            if (len > 4 && _wcsicmp(clean_name + len - 4, L".exe") == 0) {
                clean_name[len - 4] = L'\0';
            }

            HKEY hSubKey;
            wchar_t path_val[MAX_PATH] = {0};
            DWORD path_len = sizeof(path_val);
            if (RegOpenKeyExW(hKey, subkey_name, 0, KEY_READ, &hSubKey) == ERROR_SUCCESS) {
                if (RegQueryValueExW(hSubKey, NULL, NULL, NULL, (LPBYTE)path_val, &path_len) == ERROR_SUCCESS && path_val[0] != L'\0') {
                    wchar_t *target = path_val;
                    if (target[0] == L'"') {
                        target++;
                        size_t tlen = wcslen(target);
                        if (tlen > 0 && target[tlen - 1] == L'"') {
                            target[tlen - 1] = L'\0';
                        }
                    }
                    add_app_entry(clean_name, target);
                } else {
                    add_app_entry(clean_name, subkey_name);
                }
                RegCloseKey(hSubKey);
            } else {
                add_app_entry(clean_name, subkey_name);
            }

            index++;
            subkey_len = MAX_PATH;
        }
        RegCloseKey(hKey);
    }
}

static void index_all_applications(void)
{
    s_app_count = 0;

    // Common Start Menu
    wchar_t prog_data[MAX_PATH];
    if (GetEnvironmentVariableW(L"ProgramData", prog_data, MAX_PATH) > 0) {
        wchar_t start_menu[MAX_PATH];
        _snwprintf(start_menu, MAX_PATH, L"%s\\Microsoft\\Windows\\Start Menu\\Programs", prog_data);
        scan_directory_for_shortcuts(start_menu);
    }

    // User Start Menu
    wchar_t app_data[MAX_PATH];
    if (GetEnvironmentVariableW(L"APPDATA", app_data, MAX_PATH) > 0) {
        wchar_t user_start_menu[MAX_PATH];
        _snwprintf(user_start_menu, MAX_PATH, L"%s\\Microsoft\\Windows\\Start Menu\\Programs", app_data);
        scan_directory_for_shortcuts(user_start_menu);
    }

    // App Paths registry
    scan_app_paths_registry();

    // Standard built-ins
    add_app_entry(L"Chrome", L"chrome");
    add_app_entry(L"Windows Terminal", L"wt.exe");
    add_app_entry(L"Notepad", L"notepad.exe");
    add_app_entry(L"Calculatrice", L"calc.exe");
    add_app_entry(L"Command Prompt", L"cmd.exe");
    add_app_entry(L"PowerShell", L"powershell.exe");
    add_app_entry(L"VS Code", L"code");
    add_app_entry(L"Explorer", L"explorer.exe");
    add_app_entry(L"Gestionnaire des t\u00e2ches", L"taskmgr.exe");
    add_app_entry(L"Paint", L"mspaint.exe");
}

static void filter_apps(const wchar_t *query)
{
    s_match_count = 0;
    s_selected_index = 0;

    if (query == NULL) {
        return;
    }

    while (*query == L' ' || *query == L'\t') {
        query++;
    }

    if (*query == L'\0') {
        return;
    }

    size_t qlen = wcslen(query);

    for (int i = 0; i < s_app_count; i++) {
        int score = 0;
        const wchar_t *name = s_apps[i].name;
        size_t nlen = wcslen(name);

        if (wcs_starts_with_ci(name, query)) {
            score = 1000 - (int)(nlen - qlen);
        } else {
            // Check if any word starts with query
            const wchar_t *p = name;
            while (*p != L'\0') {
                while (*p == L' ' || *p == L'-' || *p == L'_') {
                    p++;
                }
                if (wcs_starts_with_ci(p, query)) {
                    score = 800 - (int)(nlen - qlen);
                    break;
                }
                while (*p != L'\0' && *p != L' ' && *p != L'-' && *p != L'_') {
                    p++;
                }
            }

            if (score == 0 && wcs_contains_ci(name, query)) {
                score = 500 - (int)(nlen - qlen);
            } else if (score == 0 && wcs_contains_ci(s_apps[i].target, query)) {
                score = 300;
            }
        }

        if (score > 0) {
            // Insert into top matches sorted by score
            int insert_pos = -1;
            for (int j = 0; j < s_match_count; j++) {
                if (score > s_matches[j].score) {
                    insert_pos = j;
                    break;
                }
            }

            if (insert_pos == -1 && s_match_count < MAX_MATCHES) {
                insert_pos = s_match_count;
            }

            if (insert_pos != -1) {
                int limit = s_match_count < MAX_MATCHES ? s_match_count : (MAX_MATCHES - 1);
                for (int k = limit; k > insert_pos; k--) {
                    s_matches[k] = s_matches[k - 1];
                }
                s_matches[insert_pos].app_index = i;
                s_matches[insert_pos].score = score;
                if (s_match_count < MAX_MATCHES) {
                    s_match_count++;
                }
            }
        }
    }
}

static void update_launcher_height(void)
{
    if (s_hwnd_launcher == NULL) {
        return;
    }

    int target_h = BASE_HEIGHT;
    if (s_match_count > 0) {
        target_h = BASE_HEIGHT + 8 + s_match_count * ITEM_HEIGHT;
    }

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
        target_h,
        SWP_NOACTIVATE
    );

    InvalidateRect(s_hwnd_launcher, NULL, FALSE);
}

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

    // Direct check for .lnk or explicit file path
    size_t in_len = wcslen(input);
    if (in_len > 4 && _wcsicmp(input + in_len - 4, L".lnk") == 0) {
        ShellExecuteW(NULL, L"open", input, NULL, NULL, SW_SHOWNORMAL);
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

    // 2. Try with .exe extension
    if (wcsstr(cmd, L".") == NULL) {
        wchar_t cmd_exe[MAX_PATH + 5];
        _snwprintf(cmd_exe, sizeof(cmd_exe) / sizeof(cmd_exe[0]), L"%s.exe", cmd);
        hInst = ShellExecuteW(NULL, L"open", cmd_exe, params, NULL, SW_SHOWNORMAL);
        if ((INT_PTR)hInst > 32) {
            return;
        }
    }

    // 3. Fallback to cmd.exe /c start "" <command>
    wchar_t cmd_args[1024];
    _snwprintf(cmd_args, sizeof(cmd_args) / sizeof(cmd_args[0]), L"/c start \"\" %s", input);
    ShellExecuteW(NULL, L"open", L"cmd.exe", cmd_args, NULL, SW_HIDE);
}

static LRESULT CALLBACK edit_subclass_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_KEYDOWN) {
        if (wParam == VK_DOWN) {
            if (s_match_count > 0) {
                s_selected_index = (s_selected_index + 1) % s_match_count;
                InvalidateRect(s_hwnd_launcher, NULL, FALSE);
            }
            return 0;
        }

        if (wParam == VK_UP) {
            if (s_match_count > 0) {
                s_selected_index = (s_selected_index - 1 + s_match_count) % s_match_count;
                InvalidateRect(s_hwnd_launcher, NULL, FALSE);
            }
            return 0;
        }

        if (wParam == VK_TAB) {
            if (s_match_count > 0 && s_selected_index >= 0 && s_selected_index < s_match_count) {
                int app_idx = s_matches[s_selected_index].app_index;
                SetWindowTextW(hwnd, s_apps[app_idx].name);
                int len = (int)wcslen(s_apps[app_idx].name);
                SendMessageW(hwnd, EM_SETSEL, len, len);
            }
            return 0;
        }

        if (wParam == VK_RETURN) {
            wchar_t target_to_exec[MAX_PATH] = {0};
            if (s_match_count > 0 && s_selected_index >= 0 && s_selected_index < s_match_count) {
                int app_idx = s_matches[s_selected_index].app_index;
                wcsncpy(target_to_exec, s_apps[app_idx].target, MAX_PATH - 1);
            } else {
                GetWindowTextW(hwnd, target_to_exec, MAX_PATH - 1);
            }
            launcher_hide();
            execute_command(target_to_exec);
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
    case WM_COMMAND: {
        if (HIWORD(wParam) == EN_CHANGE && (HWND)lParam == s_hwnd_edit) {
            wchar_t buffer[256] = {0};
            GetWindowTextW(s_hwnd_edit, buffer, 255);
            filter_apps(buffer);
            update_launcher_height();
            return 0;
        }
        break;
    }

    case WM_MOUSEMOVE: {
        int y = HIWORD(lParam);
        if (y >= BASE_HEIGHT && s_match_count > 0) {
            int hovered = (y - BASE_HEIGHT - 4) / ITEM_HEIGHT;
            if (hovered >= 0 && hovered < s_match_count && hovered != s_selected_index) {
                s_selected_index = hovered;
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
        return 0;
    }

    case WM_LBUTTONDOWN: {
        int y = HIWORD(lParam);
        if (y >= BASE_HEIGHT && s_match_count > 0) {
            int clicked = (y - BASE_HEIGHT - 4) / ITEM_HEIGHT;
            if (clicked >= 0 && clicked < s_match_count) {
                int app_idx = s_matches[clicked].app_index;
                wchar_t target[MAX_PATH];
                wcsncpy(target, s_apps[app_idx].target, MAX_PATH - 1);
                target[MAX_PATH - 1] = L'\0';
                launcher_hide();
                execute_command(target);
                return 0;
            }
        }
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT client_rect;
        GetClientRect(hwnd, &client_rect);
        int w = client_rect.right - client_rect.left;
        int h = client_rect.bottom - client_rect.top;

        // Double buffer to eliminate flicker
        HDC mem_dc = CreateCompatibleDC(hdc);
        HBITMAP mem_bmp = CreateCompatibleBitmap(hdc, w, h);
        HGDIOBJ old_bmp = SelectObject(mem_dc, mem_bmp);

        // Fill background
        FillRect(mem_dc, &client_rect, s_bg_brush);

        // Draw border
        HGDIOBJ old_pen = SelectObject(mem_dc, s_border_pen);
        HGDIOBJ old_brush = SelectObject(mem_dc, GetStockObject(HOLLOW_BRUSH));
        Rectangle(mem_dc, 0, 0, w, h);
        Rectangle(mem_dc, 1, 1, w - 1, h - 1);
        SelectObject(mem_dc, old_brush);
        SelectObject(mem_dc, old_pen);

        // Draw terminal prompt ">"
        SetBkMode(mem_dc, TRANSPARENT);
        SetTextColor(mem_dc, COLOR_ACCENT);
        HGDIOBJ old_font = SelectObject(mem_dc, s_font);
        TextOutW(mem_dc, 16, 14, L">", 1);

        // Draw prediction suggestions list if present
        if (s_match_count > 0) {
            // Horizontal separator
            HPEN sep_pen = CreatePen(PS_SOLID, 1, RGB(42, 47, 58));
            HGDIOBJ prev_pen = SelectObject(mem_dc, sep_pen);
            MoveToEx(mem_dc, 12, BASE_HEIGHT - 2, NULL);
            LineTo(mem_dc, w - 12, BASE_HEIGHT - 2);
            SelectObject(mem_dc, prev_pen);
            DeleteObject(sep_pen);

            for (int i = 0; i < s_match_count; i++) {
                int item_top = BASE_HEIGHT + 4 + i * ITEM_HEIGHT;
                int item_bottom = item_top + ITEM_HEIGHT;
                RECT item_rect = { 10, item_top, w - 10, item_bottom };

                int app_idx = s_matches[i].app_index;
                const wchar_t *name = s_apps[app_idx].name;

                if (i == s_selected_index) {
                    // Selected item background
                    HBRUSH sel_brush = CreateSolidBrush(RGB(28, 44, 36));
                    FillRect(mem_dc, &item_rect, sel_brush);
                    DeleteObject(sel_brush);

                    // Selected item left accent line
                    RECT bar_rect = { 10, item_top + 4, 14, item_bottom - 4 };
                    FillRect(mem_dc, &bar_rect, s_accent_brush);

                    // Selected indicator
                    SetTextColor(mem_dc, COLOR_ACCENT);
                    TextOutW(mem_dc, 22, item_top + 8, L"\u279c", 1);

                    // Name
                    SetTextColor(mem_dc, RGB(255, 255, 255));
                    TextOutW(mem_dc, 46, item_top + 8, name, (int)wcslen(name));
                } else {
                    // Normal item bullet
                    SetTextColor(mem_dc, RGB(90, 100, 115));
                    TextOutW(mem_dc, 24, item_top + 8, L"\u00b7", 1);

                    // Normal item name
                    SetTextColor(mem_dc, RGB(185, 195, 205));
                    TextOutW(mem_dc, 46, item_top + 8, name, (int)wcslen(name));
                }
            }
        }

        SelectObject(mem_dc, old_font);

        // Blit buffer to screen
        BitBlt(hdc, 0, 0, w, h, mem_dc, 0, 0, SRCCOPY);

        SelectObject(mem_dc, old_bmp);
        DeleteObject(mem_bmp);
        DeleteDC(mem_dc);

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
    s_accent_brush = CreateSolidBrush(COLOR_ACCENT);
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
        BASE_HEIGHT,
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

    // Index all available applications
    index_all_applications();

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

    if (s_accent_brush != NULL) {
        DeleteObject(s_accent_brush);
        s_accent_brush = NULL;
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

    s_match_count = 0;
    s_selected_index = 0;

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
        BASE_HEIGHT,
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
    s_match_count = 0;
    s_selected_index = 0;
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
