#include "launcher.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <shellapi.h>
#include <dwmapi.h>

#define LAUNCHER_WIDTH 600
#define BASE_HEIGHT 56
#define ITEM_HEIGHT 40
#define MAX_MATCHES 6
#define MAX_INDEXED_APPS 1024
#define LAUNCHER_CLASS_NAME L"KlyprLauncherClass"

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif
#ifndef DWMSBT_TRANSIENTWINDOW
#define DWMSBT_TRANSIENTWINDOW 3
#endif

typedef enum {
    ACCENT_DISABLED = 0,
    ACCENT_ENABLE_GRADIENT = 1,
    ACCENT_ENABLE_TRANSPARENTGRADIENT = 2,
    ACCENT_ENABLE_BLURBEHIND = 3,
    ACCENT_ENABLE_ACRYLICBLURBEHIND = 4,
    ACCENT_ENABLE_HOSTBACKDROP = 5
} ACCENT_STATE;

typedef struct {
    ACCENT_STATE AccentState;
    DWORD AccentFlags;
    DWORD GradientColor;
    DWORD AnimationId;
} ACCENT_POLICY;

typedef struct {
    DWORD Attribute;
    PVOID Data;
    ULONG SizeOfData;
} WINCOMPATTRDATA;

typedef BOOL (WINAPI *pfnSetWindowCompositionAttribute)(HWND, WINCOMPATTRDATA *);

typedef struct {
    wchar_t name[128];
    wchar_t target[MAX_PATH];
    wchar_t desc[64];
    bool is_action;
} AppEntry;

typedef struct {
    int app_index;
    int score;
} MatchEntry;

typedef struct {
    COLORREF bg_color;
    COLORREF card_bg_color;
    COLORREF border_color;
    COLORREF text_primary;
    COLORREF text_secondary;
    COLORREF accent_color;
    COLORREF sep_color;
    BYTE opacity;
    bool is_dark;
    const wchar_t *font_name;
    const wchar_t *prompt_symbol;
} ThemePalette;

static HWND s_hwnd_launcher = NULL;
static HWND s_hwnd_edit = NULL;
static WNDPROC s_old_edit_proc = NULL;
static HFONT s_font = NULL;
static HBRUSH s_bg_brush = NULL;
static HBRUSH s_card_brush = NULL;
static HBRUSH s_accent_brush = NULL;
static HPEN s_border_pen = NULL;
static bool s_is_visible = false;
static ThemePalette s_current_theme;

static AppEntry s_apps[MAX_INDEXED_APPS];
static int s_app_count = 0;

static MatchEntry s_matches[MAX_MATCHES];
static int s_match_count = 0;
static int s_selected_index = 0;
static AppEntry s_url_entry;

static const struct {
    const wchar_t *name;
    const wchar_t *target;
    const wchar_t *desc;
} s_theme_actions[] = {
    { L"Th\u00e8me: Syst\u00e8me (Auto Windows)", L"__theme:0", L"S'adapte automatiquement \u00e0 Windows" },
    { L"Th\u00e8me: Sombre (Fluent Dark)", L"__theme:1", L"Design moderne sombre et translucide" },
    { L"Th\u00e8me: Clair (Fluent Light)", L"__theme:2", L"Design moderne clair et translucide" },
    { L"Th\u00e8me: Cyberpunk (Terminal)", L"__theme:3", L"Terminal hacker vert n\u00e9on sur fond noir" },
    { L"Th\u00e8me: Dracula (Violet)", L"__theme:4", L"Th\u00e8me d\u00e9veloppeur violet et cyan" }
};

static bool is_system_dark_theme(void)
{
    HKEY hKey;
    DWORD value = 0;
    DWORD size = sizeof(value);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        if (RegQueryValueExW(hKey, L"AppsUseLightTheme", NULL, NULL, (LPBYTE)&value, &size) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return value == 0;
        }
        RegCloseKey(hKey);
    }
    return true;
}

static void apply_window_blur(HWND hwnd, COLORREF bg_color, BYTE opacity, bool is_dark)
{
    // 1. DWM Window Attributes (Windows 11 modern rounded corners & acrylic backdrop)
    DWORD corner_pref = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner_pref, sizeof(corner_pref));

    DWORD dark_flag = is_dark ? 1 : 0;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark_flag, sizeof(dark_flag));

    DWORD backdrop = DWMSBT_TRANSIENTWINDOW;
    DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));

    // 2. SetWindowCompositionAttribute (Acrylic blur for Windows 10 & 11)
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 != NULL) {
        pfnSetWindowCompositionAttribute pSetWindowCompositionAttribute =
            (pfnSetWindowCompositionAttribute)(void *)GetProcAddress(user32, "SetWindowCompositionAttribute");
        if (pSetWindowCompositionAttribute != NULL) {
            ACCENT_POLICY policy = {0};
            policy.AccentState = ACCENT_ENABLE_ACRYLICBLURBEHIND;
            BYTE a = (BYTE)(opacity > 40 ? opacity - 40 : opacity);
            BYTE r = GetRValue(bg_color);
            BYTE g = GetGValue(bg_color);
            BYTE b = GetBValue(bg_color);
            policy.GradientColor = ((DWORD)a << 24) | ((DWORD)b << 16) | ((DWORD)g << 8) | (DWORD)r;

            WINCOMPATTRDATA data = {0};
            data.Attribute = 19;
            data.Data = &policy;
            data.SizeOfData = sizeof(policy);
            pSetWindowCompositionAttribute(hwnd, &data);
        }
    }

    // 3. Layered Window Attributes (Alpha transparency)
    SetLayeredWindowAttributes(hwnd, 0, opacity, LWA_ALPHA);
}

void launcher_apply_theme(void)
{
    ThemeType active = g_config.theme;
    if (active == THEME_SYSTEM) {
        active = is_system_dark_theme() ? THEME_DARK : THEME_LIGHT;
    }

    BYTE target_opacity = (BYTE)(g_config.opacity * 255 / 100);

    switch (active) {
    case THEME_LIGHT:
        s_current_theme.bg_color = RGB(246, 248, 252);
        s_current_theme.card_bg_color = RGB(225, 230, 238);
        s_current_theme.border_color = RGB(205, 212, 222);
        s_current_theme.text_primary = RGB(24, 28, 34);
        s_current_theme.text_secondary = RGB(105, 115, 128);
        s_current_theme.accent_color = RGB(0, 103, 192);
        s_current_theme.sep_color = RGB(220, 226, 235);
        s_current_theme.opacity = target_opacity;
        s_current_theme.is_dark = false;
        s_current_theme.font_name = L"Segoe UI";
        s_current_theme.prompt_symbol = L"\u2315";
        break;

    case THEME_CYBERPUNK:
        s_current_theme.bg_color = RGB(12, 14, 16);
        s_current_theme.card_bg_color = RGB(18, 36, 24);
        s_current_theme.border_color = RGB(0, 230, 118);
        s_current_theme.text_primary = RGB(230, 255, 240);
        s_current_theme.text_secondary = RGB(0, 190, 95);
        s_current_theme.accent_color = RGB(0, 230, 118);
        s_current_theme.sep_color = RGB(25, 48, 35);
        s_current_theme.opacity = target_opacity;
        s_current_theme.is_dark = true;
        s_current_theme.font_name = L"Consolas";
        s_current_theme.prompt_symbol = L">";
        break;

    case THEME_DRACULA:
        s_current_theme.bg_color = RGB(40, 42, 54);
        s_current_theme.card_bg_color = RGB(68, 71, 90);
        s_current_theme.border_color = RGB(189, 147, 249);
        s_current_theme.text_primary = RGB(248, 248, 242);
        s_current_theme.text_secondary = RGB(139, 233, 253);
        s_current_theme.accent_color = RGB(189, 147, 249);
        s_current_theme.sep_color = RGB(68, 71, 90);
        s_current_theme.opacity = target_opacity;
        s_current_theme.is_dark = true;
        s_current_theme.font_name = L"Segoe UI";
        s_current_theme.prompt_symbol = L"\u2726";
        break;

    case THEME_DARK:
    default:
        s_current_theme.bg_color = RGB(30, 32, 38);
        s_current_theme.card_bg_color = RGB(48, 52, 64);
        s_current_theme.border_color = RGB(62, 66, 78);
        s_current_theme.text_primary = RGB(246, 247, 249);
        s_current_theme.text_secondary = RGB(150, 158, 172);
        s_current_theme.accent_color = RGB(96, 205, 255);
        s_current_theme.sep_color = RGB(50, 54, 66);
        s_current_theme.opacity = target_opacity;
        s_current_theme.is_dark = true;
        s_current_theme.font_name = L"Segoe UI";
        s_current_theme.prompt_symbol = L"\u2315";
        break;
    }

    if (s_bg_brush != NULL) DeleteObject(s_bg_brush);
    if (s_card_brush != NULL) DeleteObject(s_card_brush);
    if (s_accent_brush != NULL) DeleteObject(s_accent_brush);
    if (s_border_pen != NULL) DeleteObject(s_border_pen);
    if (s_font != NULL) DeleteObject(s_font);

    s_bg_brush = CreateSolidBrush(s_current_theme.bg_color);
    s_card_brush = CreateSolidBrush(s_current_theme.card_bg_color);
    s_accent_brush = CreateSolidBrush(s_current_theme.accent_color);
    s_border_pen = CreatePen(PS_SOLID, 1, s_current_theme.border_color);

    s_font = CreateFontW(
        21,
        0,
        0,
        0,
        FW_NORMAL,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_OUTLINE_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        s_current_theme.font_name
    );

    if (s_hwnd_launcher != NULL) {
        apply_window_blur(s_hwnd_launcher, s_current_theme.bg_color, s_current_theme.opacity, s_current_theme.is_dark);
        if (s_hwnd_edit != NULL) {
            SendMessageW(s_hwnd_edit, WM_SETFONT, (WPARAM)s_font, TRUE);
        }
        InvalidateRect(s_hwnd_launcher, NULL, TRUE);
    }
}

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

static void add_app_entry(const wchar_t *name, const wchar_t *target, const wchar_t *desc, bool is_action)
{
    if (name == NULL || target == NULL || name[0] == L'\0' || s_app_count >= MAX_INDEXED_APPS) {
        return;
    }

    if (!is_action) {
        if (wcs_contains_ci(name, L"uninstall") ||
            wcs_contains_ci(name, L"d\u00e9sinstall") ||
            wcs_contains_ci(name, L"desinstall") ||
            wcs_contains_ci(name, L"readme") ||
            wcs_contains_ci(name, L"aide") ||
            wcs_contains_ci(name, L"help") ||
            wcs_contains_ci(name, L"website")) {
            return;
        }

        for (int i = 0; i < s_app_count; i++) {
            if (!s_apps[i].is_action && _wcsicmp(s_apps[i].name, name) == 0) {
                return;
            }
        }
    }

    wcsncpy(s_apps[s_app_count].name, name, 127);
    s_apps[s_app_count].name[127] = L'\0';
    wcsncpy(s_apps[s_app_count].target, target, MAX_PATH - 1);
    s_apps[s_app_count].target[MAX_PATH - 1] = L'\0';
    wcsncpy(s_apps[s_app_count].desc, desc ? desc : L"Application", 63);
    s_apps[s_app_count].desc[63] = L'\0';
    s_apps[s_app_count].is_action = is_action;
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
                add_app_entry(base_name, full_path, L"Application", false);
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
                    add_app_entry(clean_name, target, L"Application", false);
                } else {
                    add_app_entry(clean_name, subkey_name, L"Application", false);
                }
                RegCloseKey(hSubKey);
            } else {
                add_app_entry(clean_name, subkey_name, L"Application", false);
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

    // 1. Add Theme Configuration Actions
    for (size_t i = 0; i < sizeof(s_theme_actions) / sizeof(s_theme_actions[0]); i++) {
        add_app_entry(s_theme_actions[i].name, s_theme_actions[i].target, s_theme_actions[i].desc, true);
    }

    // 2. Start Menu shortcuts
    wchar_t prog_data[MAX_PATH];
    if (GetEnvironmentVariableW(L"ProgramData", prog_data, MAX_PATH) > 0) {
        wchar_t start_menu[MAX_PATH];
        _snwprintf(start_menu, MAX_PATH, L"%s\\Microsoft\\Windows\\Start Menu\\Programs", prog_data);
        scan_directory_for_shortcuts(start_menu);
    }

    wchar_t app_data[MAX_PATH];
    if (GetEnvironmentVariableW(L"APPDATA", app_data, MAX_PATH) > 0) {
        wchar_t user_start_menu[MAX_PATH];
        _snwprintf(user_start_menu, MAX_PATH, L"%s\\Microsoft\\Windows\\Start Menu\\Programs", app_data);
        scan_directory_for_shortcuts(user_start_menu);
    }

    // 3. App Paths Registry
    scan_app_paths_registry();

    // 4. Built-in system apps
    add_app_entry(L"Chrome", L"chrome", L"Navigateur Web", false);
    add_app_entry(L"Windows Terminal", L"wt.exe", L"Terminal", false);
    add_app_entry(L"Notepad", L"notepad.exe", L"Bloc-notes", false);
    add_app_entry(L"Calculatrice", L"calc.exe", L"Utilitaire", false);
    add_app_entry(L"Command Prompt", L"cmd.exe", L"Invite de commandes", false);
    add_app_entry(L"PowerShell", L"powershell.exe", L"Terminal PowerShell", false);
    add_app_entry(L"VS Code", L"code", L"Editeur de code", false);
    add_app_entry(L"Explorer", L"explorer.exe", L"Gestionnaire de fichiers", false);
    add_app_entry(L"Gestionnaire des t\u00e2ches", L"taskmgr.exe", L"Syst\u00e8me", false);
    add_app_entry(L"Paint", L"mspaint.exe", L"Graphisme", false);
}

static bool is_likely_url(const wchar_t *str, wchar_t *out_url, size_t out_url_size)
{
    if (str == NULL || out_url == NULL || out_url_size < 10) {
        return false;
    }

    while (*str == L' ' || *str == L'\t') {
        str++;
    }

    if (*str == L'\0') {
        return false;
    }

    // A URL cannot contain spaces
    if (wcschr(str, L' ') != NULL || wcschr(str, L'\t') != NULL) {
        return false;
    }

    // 1. Explicit http:// or https://
    if (wcs_starts_with_ci(str, L"http://") || wcs_starts_with_ci(str, L"https://")) {
        wcsncpy(out_url, str, out_url_size - 1);
        out_url[out_url_size - 1] = L'\0';
        return true;
    }

    // 2. Starts with www.
    if (wcs_starts_with_ci(str, L"www.")) {
        _snwprintf(out_url, out_url_size, L"https://%s", str);
        return true;
    }

    // 3. Starts with localhost
    if (wcs_starts_with_ci(str, L"localhost:") || _wcsicmp(str, L"localhost") == 0 || wcs_starts_with_ci(str, L"localhost/")) {
        _snwprintf(out_url, out_url_size, L"http://%s", str);
        return true;
    }

    // 4. Domain / IP detection
    const wchar_t *first_slash = wcschr(str, L'/');
    const wchar_t *first_colon = wcschr(str, L':');
    const wchar_t *first_delim = first_slash;
    if (first_colon != NULL && (first_delim == NULL || first_colon < first_delim)) {
        first_delim = first_colon;
    }

    wchar_t host[128];
    size_t host_len = first_delim ? (size_t)(first_delim - str) : wcslen(str);
    if (host_len == 0 || host_len >= 128) {
        return false;
    }
    wcsncpy(host, str, host_len);
    host[host_len] = L'\0';

    const wchar_t *last_dot = wcsrchr(host, L'.');
    if (last_dot == NULL || last_dot == host || *(last_dot + 1) == L'\0') {
        return false;
    }

    const wchar_t *tld = last_dot + 1;
    size_t tld_len = wcslen(tld);
    if (tld_len < 2 || tld_len > 12) {
        return false;
    }

    bool all_digits = true;
    for (size_t i = 0; i < tld_len; i++) {
        if (!iswdigit(tld[i])) {
            all_digits = false;
        }
        if (!iswalpha(tld[i]) && !iswdigit(tld[i])) {
            return false;
        }
    }

    if (!all_digits) {
        static const wchar_t *non_web_exts[] = {
            L"exe", L"bat", L"cmd", L"lnk", L"dll", L"sys", L"msi",
            L"txt", L"doc", L"docx", L"pdf", L"zip", L"tar", L"gz", L"7z", L"rar",
            L"c", L"h", L"cpp", L"hpp", L"py", L"json", L"xml", L"ini", L"cfg", L"log",
            L"png", L"jpg", L"jpeg", L"gif", L"ico", L"mp3", L"mp4", L"mkv"
        };
        for (size_t i = 0; i < sizeof(non_web_exts) / sizeof(non_web_exts[0]); i++) {
            if (_wcsicmp(tld, non_web_exts[i]) == 0) {
                return false;
            }
        }
    }

    if (all_digits) {
        _snwprintf(out_url, out_url_size, L"http://%s", str);
    } else {
        _snwprintf(out_url, out_url_size, L"https://%s", str);
    }
    return true;
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

        // Check if query is targeting themes (e.g. "th", "theme", "dark", "light", etc.)
        if (s_apps[i].is_action) {
            if (wcs_contains_ci(name, query) || wcs_contains_ci(s_apps[i].desc, query)) {
                score = 900 - (int)(nlen - qlen);
            }
        } else {
            if (wcs_starts_with_ci(name, query)) {
                score = 1000 - (int)(nlen - qlen);
            } else {
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
        }

        if (score > 0) {
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

    if (s_match_count == 0) {
        wchar_t normalized_url[MAX_PATH];
        if (is_likely_url(query, normalized_url, MAX_PATH)) {
            wcsncpy(s_url_entry.name, normalized_url, 127);
            s_url_entry.name[127] = L'\0';
            wcsncpy(s_url_entry.target, normalized_url, MAX_PATH - 1);
            s_url_entry.target[MAX_PATH - 1] = L'\0';
            wcsncpy(s_url_entry.desc, L"Navigateur Web", 63);
            s_url_entry.desc[63] = L'\0';
            s_url_entry.is_action = true;

            s_matches[0].app_index = -1;
            s_matches[0].score = 1000;
            s_match_count = 1;
            s_selected_index = 0;
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

    // Check if this is an internal theme switch action
    if (wcsncmp(input, L"__theme:", 8) == 0) {
        int theme_id = _wtoi(input + 8);
        config_set_theme((ThemeType)theme_id);
        launcher_apply_theme();
        return;
    }

    // Direct check if input is a web URL/link
    wchar_t normalized_url[MAX_PATH];
    if (is_likely_url(input, normalized_url, MAX_PATH)) {
        ShellExecuteW(NULL, L"open", normalized_url, NULL, NULL, SW_SHOWNORMAL);
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
                const wchar_t *name = (app_idx == -1) ? s_url_entry.name : s_apps[app_idx].name;
                SetWindowTextW(hwnd, name);
                int len = (int)wcslen(name);
                SendMessageW(hwnd, EM_SETSEL, len, len);
            }
            return 0;
        }

        if (wParam == VK_RETURN) {
            wchar_t target_to_exec[MAX_PATH] = {0};
            if (s_match_count > 0 && s_selected_index >= 0 && s_selected_index < s_match_count) {
                int app_idx = s_matches[s_selected_index].app_index;
                const wchar_t *target = (app_idx == -1) ? s_url_entry.target : s_apps[app_idx].target;
                wcsncpy(target_to_exec, target, MAX_PATH - 1);
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
                const wchar_t *target = (app_idx == -1) ? s_url_entry.target : s_apps[app_idx].target;
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

        // Double buffer for 100% flicker-free rendering
        HDC mem_dc = CreateCompatibleDC(hdc);
        HBITMAP mem_bmp = CreateCompatibleBitmap(hdc, w, h);
        HGDIOBJ old_bmp = SelectObject(mem_dc, mem_bmp);

        // Fill background
        FillRect(mem_dc, &client_rect, s_bg_brush);

        // Draw soft outer border
        HGDIOBJ old_pen = SelectObject(mem_dc, s_border_pen);
        HGDIOBJ old_brush = SelectObject(mem_dc, GetStockObject(HOLLOW_BRUSH));
        RoundRect(mem_dc, 0, 0, w, h, 16, 16);
        SelectObject(mem_dc, old_brush);
        SelectObject(mem_dc, old_pen);

        // Draw prompt icon
        SetBkMode(mem_dc, TRANSPARENT);
        SetTextColor(mem_dc, s_current_theme.accent_color);
        HGDIOBJ old_font = SelectObject(mem_dc, s_font);
        TextOutW(mem_dc, 18, 16, s_current_theme.prompt_symbol, (int)wcslen(s_current_theme.prompt_symbol));

        // Draw suggestion dropdown items if active
        if (s_match_count > 0) {
            // Subtle separator line
            HPEN sep_pen = CreatePen(PS_SOLID, 1, s_current_theme.sep_color);
            HGDIOBJ prev_pen = SelectObject(mem_dc, sep_pen);
            MoveToEx(mem_dc, 16, BASE_HEIGHT - 2, NULL);
            LineTo(mem_dc, w - 16, BASE_HEIGHT - 2);
            SelectObject(mem_dc, prev_pen);
            DeleteObject(sep_pen);

            for (int i = 0; i < s_match_count; i++) {
                int item_top = BASE_HEIGHT + 4 + i * ITEM_HEIGHT;
                int item_bottom = item_top + ITEM_HEIGHT - 2;

                int app_idx = s_matches[i].app_index;
                const wchar_t *name = (app_idx == -1) ? s_url_entry.name : s_apps[app_idx].name;
                const wchar_t *desc = (app_idx == -1) ? s_url_entry.desc : s_apps[app_idx].desc;

                if (i == s_selected_index) {
                    // Modern rounded selection card/pill
                    HGDIOBJ prev_card_brush = SelectObject(mem_dc, s_card_brush);
                    HGDIOBJ null_pen = SelectObject(mem_dc, GetStockObject(NULL_PEN));
                    RoundRect(mem_dc, 10, item_top, w - 10, item_bottom, 10, 10);

                    // Left accent indicator bar
                    RECT bar_rect = { 12, item_top + 6, 16, item_bottom - 6 };
                    FillRect(mem_dc, &bar_rect, s_accent_brush);

                    SelectObject(mem_dc, null_pen);
                    SelectObject(mem_dc, prev_card_brush);

                    // Indicator arrow
                    SetTextColor(mem_dc, s_current_theme.accent_color);
                    TextOutW(mem_dc, 26, item_top + 9, L"\u279c", 1);

                    // Primary name
                    SetTextColor(mem_dc, s_current_theme.text_primary);
                    TextOutW(mem_dc, 50, item_top + 9, name, (int)wcslen(name));

                    // Secondary description on right
                    SetTextColor(mem_dc, s_current_theme.accent_color);
                    SIZE desc_size;
                    GetTextExtentPoint32W(mem_dc, desc, (int)wcslen(desc), &desc_size);
                    TextOutW(mem_dc, w - 24 - desc_size.cx, item_top + 9, desc, (int)wcslen(desc));
                } else {
                    // Unselected item bullet
                    SetTextColor(mem_dc, s_current_theme.text_secondary);
                    TextOutW(mem_dc, 28, item_top + 9, L"\u00b7", 1);

                    // Primary name
                    SetTextColor(mem_dc, s_current_theme.text_primary);
                    TextOutW(mem_dc, 50, item_top + 9, name, (int)wcslen(name));

                    // Secondary description on right
                    SetTextColor(mem_dc, s_current_theme.text_secondary);
                    SIZE desc_size;
                    GetTextExtentPoint32W(mem_dc, desc, (int)wcslen(desc), &desc_size);
                    TextOutW(mem_dc, w - 24 - desc_size.cx, item_top + 9, desc, (int)wcslen(desc));
                }
            }
        }

        SelectObject(mem_dc, old_font);

        // Blit buffer to window
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
        SetTextColor(hdc, s_current_theme.text_primary);
        SetBkColor(hdc, s_current_theme.bg_color);
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

    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = launcher_wnd_proc;
    wc.hInstance = hInstance;
    wc.lpszClassName = LAUNCHER_CLASS_NAME;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);

    if (!RegisterClassExW(&wc)) {
        return false;
    }

    int screen_w = GetSystemMetrics(SM_CXSCREEN);
    int screen_h = GetSystemMetrics(SM_CYSCREEN);
    int pos_x = (screen_w - LAUNCHER_WIDTH) / 2;
    int pos_y = screen_h / 4;

    s_hwnd_launcher = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
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
        48,
        15,
        LAUNCHER_WIDTH - 68,
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

    s_old_edit_proc = (WNDPROC)SetWindowLongPtrW(
        s_hwnd_edit,
        GWLP_WNDPROC,
        (LONG_PTR)edit_subclass_proc
    );

    // Index all applications and themes
    index_all_applications();

    // Apply active theme and backdrop blur
    launcher_apply_theme();

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

    if (s_card_brush != NULL) {
        DeleteObject(s_card_brush);
        s_card_brush = NULL;
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

    // Refresh theme in case system dark/light mode changed
    if (g_config.theme == THEME_SYSTEM) {
        launcher_apply_theme();
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
