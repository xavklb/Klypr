#include "launcher.h"
#include "config.h"
#include "input.h"
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
    wchar_t target[ALIAS_TARGET_LEN];
    wchar_t desc[128];
    bool is_action;
    HICON hicon;
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
#define MAX_DYNAMIC_ENTRIES 64
static AppEntry s_dynamic_entries[MAX_DYNAMIC_ENTRIES];
static int s_dyn_count = 0;

static const AppEntry *get_entry_for_match(int match_idx)
{
    if (match_idx < 0 || match_idx >= s_match_count) {
        return NULL;
    }
    int app_idx = s_matches[match_idx].app_index;
    if (app_idx >= 0 && app_idx < s_app_count) {
        return &s_apps[app_idx];
    } else if (app_idx < 0) {
        int dyn_idx = -app_idx - 1;
        if (dyn_idx >= 0 && dyn_idx < s_dyn_count) {
            return &s_dynamic_entries[dyn_idx];
        }
    }
    return NULL;
}

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

static bool wcs_ends_with_ci(const wchar_t *str, const wchar_t *suffix)
{
    if (str == NULL || suffix == NULL) {
        return false;
    }
    size_t slen = wcslen(str);
    size_t xlen = wcslen(suffix);
    if (slen < xlen) {
        return false;
    }
    for (size_t i = 0; i < xlen; i++) {
        if (towlower(str[slen - xlen + i]) != towlower(suffix[i])) {
            return false;
        }
    }
    return true;
}

static int compute_fuzzy_score(const wchar_t *pattern, const wchar_t *str)
{
    if (pattern == NULL || str == NULL || *pattern == L'\0' || *str == L'\0') {
        return 0;
    }

    size_t plen = wcslen(pattern);
    size_t slen = wcslen(str);
    if (plen > slen) {
        return 0;
    }

    // Quick subsequence check
    const wchar_t *p_check = pattern;
    const wchar_t *s_check = str;
    while (*p_check != L'\0' && *s_check != L'\0') {
        if (towlower(*p_check) == towlower(*s_check)) {
            p_check++;
        }
        s_check++;
    }
    if (*p_check != L'\0') {
        return 0;
    }

    // Boundary-preferring greedy scan
    int score = 0;
    int consecutive = 0;
    size_t s_idx = 0;

    for (size_t p_idx = 0; p_idx < plen; p_idx++) {
        wchar_t pc = towlower(pattern[p_idx]);
        size_t best_pos = (size_t)-1;
        int best_pos_score = -1000;

        for (size_t j = s_idx; j < slen; j++) {
            if (towlower(str[j]) == pc) {
                int pos_score = 0;
                bool is_wb = (j == 0) ||
                             (str[j - 1] == L' ' || str[j - 1] == L'\t' || str[j - 1] == L'-' ||
                              str[j - 1] == L'_' || str[j - 1] == L'.' || str[j - 1] == L'/' || str[j - 1] == L'\\') ||
                             (iswlower(str[j - 1]) && iswupper(str[j]));

                if (j == 0) {
                    pos_score = 80;
                } else if (is_wb) {
                    pos_score = 60;
                } else if (j == s_idx && consecutive > 0) {
                    pos_score = 40;
                } else {
                    pos_score = 15 - (int)(j - s_idx);
                }

                if (is_wb || j == 0) {
                    best_pos = j;
                    best_pos_score = pos_score;
                    break;
                }

                if (pos_score > best_pos_score) {
                    best_pos = j;
                    best_pos_score = pos_score;
                }
            }
        }

        if (best_pos == (size_t)-1) {
            return 0;
        }

        if (best_pos == s_idx && p_idx > 0) {
            consecutive++;
        } else {
            consecutive = 0;
        }

        score += best_pos_score;
        s_idx = best_pos + 1;
    }

    int length_penalty = (int)(slen - plen);
    if (length_penalty > 50) {
        length_penalty = 50;
    }

    int final_score = 700 + score - length_penalty;
    return (final_score > 1) ? final_score : 1;
}

static HICON resolve_icon(const wchar_t *raw_target, bool is_action)
{
    if (raw_target == NULL || raw_target[0] == L'\0') {
        return NULL;
    }

    while (*raw_target == L' ' || *raw_target == L'\t') {
        raw_target++;
    }

    // 1. Internal actions
    if (is_action) {
        if (wcsncmp(raw_target, L"__theme:", 8) == 0) {
            HICON h = NULL;
            if (ExtractIconExW(L"shell32.dll", 33, NULL, &h, 1) > 0 && h != NULL) {
                return h;
            }
        } else if (wcsncmp(raw_target, L"__autostart:", 12) == 0) {
            HICON h = NULL;
            if (ExtractIconExW(L"shell32.dll", 238, NULL, &h, 1) > 0 && h != NULL) {
                return h;
            }
        } else if (wcsncmp(raw_target, L"__config:", 9) == 0) {
            HICON h = NULL;
            if (ExtractIconExW(L"shell32.dll", 21, NULL, &h, 1) > 0 && h != NULL) {
                return h;
            }
        } else if (wcsncmp(raw_target, L"__alias_add:", 12) == 0) {
            const wchar_t *payload = raw_target + 12;
            const wchar_t *sep = wcschr(payload, L':');
            if (sep != NULL) {
                return resolve_icon(sep + 1, false);
            }
        } else if (wcsncmp(raw_target, L"__alias_del:", 12) == 0) {
            HICON h = NULL;
            if (ExtractIconExW(L"shell32.dll", 131, NULL, &h, 1) > 0 && h != NULL) {
                return h;
            }
        }
    }

    // 2. Web URLs
    if (wcs_starts_with_ci(raw_target, L"http://") || wcs_starts_with_ci(raw_target, L"https://") || wcs_starts_with_ci(raw_target, L"www.")) {
        HICON h = NULL;
        if (ExtractIconExW(L"shell32.dll", 13, NULL, &h, 1) > 0 && h != NULL) {
            return h;
        }
    }

    // Extract target path (strip quotes if any)
    wchar_t target[MAX_PATH];
    if (raw_target[0] == L'"') {
        const wchar_t *end_q = wcschr(raw_target + 1, L'"');
        if (end_q != NULL) {
            size_t len = (size_t)(end_q - (raw_target + 1));
            if (len >= MAX_PATH) len = MAX_PATH - 1;
            wcsncpy(target, raw_target + 1, len);
            target[len] = L'\0';
        } else {
            wcsncpy(target, raw_target + 1, MAX_PATH - 1);
            target[MAX_PATH - 1] = L'\0';
        }
    } else {
        wcsncpy(target, raw_target, MAX_PATH - 1);
        target[MAX_PATH - 1] = L'\0';
    }

    // 3. Local Directory / Folder
    DWORD attrs = GetFileAttributesW(target);
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        HICON h = NULL;
        if (ExtractIconExW(L"shell32.dll", 3, NULL, &h, 1) > 0 && h != NULL) {
            return h;
        }
    }

    // 4. Exact path or .lnk file on disk
    SHFILEINFOW sfi = {0};
    if (SHGetFileInfoW(target, 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_SMALLICON) && sfi.hIcon != NULL) {
        return sfi.hIcon;
    }

    // If target has arguments (e.g. "explorer.exe C:\..."), try first word
    wchar_t first_cmd[MAX_PATH];
    wcsncpy(first_cmd, target, MAX_PATH - 1);
    first_cmd[MAX_PATH - 1] = L'\0';
    wchar_t *sp = wcschr(first_cmd, L' ');
    if (sp != NULL) {
        *sp = L'\0';
        if (SHGetFileInfoW(first_cmd, 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_SMALLICON) && sfi.hIcon != NULL) {
            return sfi.hIcon;
        }
    }

    const wchar_t *cmd_to_lookup = (sp != NULL) ? first_cmd : target;

    // 5. App name or executable without path (via SearchPathW)
    wchar_t resolved[MAX_PATH];
    if (SearchPathW(NULL, cmd_to_lookup, L".exe", MAX_PATH, resolved, NULL) > 0) {
        memset(&sfi, 0, sizeof(sfi));
        if (SHGetFileInfoW(resolved, 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_SMALLICON) && sfi.hIcon != NULL) {
            return sfi.hIcon;
        }
    }

    // 6. Check App Paths in registry (HKLM & HKCU)
    wchar_t exe_name[MAX_PATH];
    if (wcsstr(cmd_to_lookup, L".") == NULL) {
        _snwprintf(exe_name, MAX_PATH, L"%s.exe", cmd_to_lookup);
    } else {
        wcsncpy(exe_name, cmd_to_lookup, MAX_PATH - 1);
        exe_name[MAX_PATH - 1] = L'\0';
    }

    wchar_t app_path_key[MAX_PATH];
    _snwprintf(app_path_key, MAX_PATH, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\%s", exe_name);
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, app_path_key, 0, KEY_READ, &hKey) == ERROR_SUCCESS ||
        RegOpenKeyExW(HKEY_CURRENT_USER, app_path_key, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        wchar_t reg_path[MAX_PATH] = {0};
        DWORD rsize = sizeof(reg_path);
        if (RegQueryValueExW(hKey, NULL, NULL, NULL, (LPBYTE)reg_path, &rsize) == ERROR_SUCCESS && reg_path[0] != L'\0') {
            wchar_t *p = reg_path;
            if (*p == L'"') {
                p++;
                wchar_t *end = wcsrchr(p, L'"');
                if (end) *end = L'\0';
            }
            memset(&sfi, 0, sizeof(sfi));
            if (SHGetFileInfoW(p, 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_SMALLICON) && sfi.hIcon != NULL) {
                RegCloseKey(hKey);
                return sfi.hIcon;
            }
        }
        RegCloseKey(hKey);
    }

    // 7. General extension fallback (SHGFI_USEFILEATTRIBUTES)
    memset(&sfi, 0, sizeof(sfi));
    if (SHGetFileInfoW(cmd_to_lookup, FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES) && sfi.hIcon != NULL) {
        return sfi.hIcon;
    }

    // 8. Default generic application icon
    HICON default_icon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
    if (default_icon != NULL) {
        return CopyIcon(default_icon);
    }

    return NULL;
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
    wcsncpy(s_apps[s_app_count].target, target, ALIAS_TARGET_LEN - 1);
    s_apps[s_app_count].target[ALIAS_TARGET_LEN - 1] = L'\0';
    wcsncpy(s_apps[s_app_count].desc, desc ? desc : L"Application", 127);
    s_apps[s_app_count].desc[127] = L'\0';
    s_apps[s_app_count].is_action = is_action;
    s_apps[s_app_count].hicon = resolve_icon(target, is_action);
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
    for (int i = 0; i < s_app_count; i++) {
        if (s_apps[i].hicon != NULL) {
            DestroyIcon(s_apps[i].hicon);
            s_apps[i].hicon = NULL;
        }
    }
    s_app_count = 0;

    // 1. Add Theme and Configuration Actions
    add_app_entry(L"Configuration: \u00c9diter klypr.ini (Alias, Th\u00e8mes...)", L"__config:open", L"Ouvrir klypr.ini dans le Bloc-notes", true);

    for (size_t i = 0; i < sizeof(s_theme_actions) / sizeof(s_theme_actions[0]); i++) {
        add_app_entry(s_theme_actions[i].name, s_theme_actions[i].target, s_theme_actions[i].desc, true);
    }

    // Autostart configuration action
    if (g_config.autostart) {
        add_app_entry(L"D\u00e9marrage: D\u00e9sactiver au d\u00e9marrage de Windows", L"__autostart:0", L"D\u00e9sactiver le lancement automatique", true);
    } else {
        add_app_entry(L"D\u00e9marrage: Activer au d\u00e9marrage de Windows", L"__autostart:1", L"Lancer Klypr au d\u00e9marrage de Windows", true);
    }

    // 2. Add User-defined Aliases (take precedence over shortcuts)
    config_load_aliases();
    for (int i = 0; i < g_config.alias_count; i++) {
        wchar_t desc[128];
        _snwprintf(desc, sizeof(desc) / sizeof(desc[0]), L"Alias \u2794 %s", g_config.aliases[i].target);
        add_app_entry(g_config.aliases[i].name, g_config.aliases[i].target, desc, false);
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

static void url_encode(const wchar_t *src, wchar_t *dst, size_t dst_size)
{
    if (src == NULL || dst == NULL || dst_size == 0) {
        return;
    }
    size_t d = 0;
    while (*src != L'\0' && d + 4 < dst_size) {
        wchar_t c = *src;
        if ((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') ||
            c == L'-' || c == L'_' || c == L'.' || c == L'~') {
            dst[d++] = c;
        } else if (c == L' ') {
            dst[d++] = L'+';
        } else if (c < 128) {
            d += (size_t)_snwprintf(dst + d, dst_size - d, L"%%%02X", (unsigned int)c);
        } else {
            char utf8_buf[8] = {0};
            WideCharToMultiByte(CP_UTF8, 0, src, 1, utf8_buf, sizeof(utf8_buf), NULL, NULL);
            for (int i = 0; utf8_buf[i] != '\0' && d + 4 < dst_size; i++) {
                d += (size_t)_snwprintf(dst + d, dst_size - d, L"%%%02X", (unsigned char)utf8_buf[i]);
            }
        }
        src++;
    }
    dst[d] = L'\0';
}

static void unquote_str(wchar_t *str)
{
    if (str == NULL) {
        return;
    }
    size_t len = wcslen(str);
    if (len >= 2 && str[0] == L'"' && str[len - 1] == L'"') {
        str[len - 1] = L'\0';
        memmove(str, str + 1, (len - 1) * sizeof(wchar_t));
    } else if (str[0] == L'"') {
        memmove(str, str + 1, len * sizeof(wchar_t));
    }
}

static bool is_popular_app(const wchar_t *name)
{
    static const wchar_t *popular[] = {
        L"Chrome", L"Terminal", L"Code", L"Notepad", L"Explorer", L"PowerShell", L"Firefox", L"Edge"
    };
    for (size_t i = 0; i < sizeof(popular) / sizeof(popular[0]); i++) {
        if (wcs_contains_ci(name, popular[i])) {
            return true;
        }
    }
    return false;
}

static void add_dynamic_match(const wchar_t *name, const wchar_t *target, const wchar_t *desc, int score)
{
    if (s_dyn_count >= MAX_DYNAMIC_ENTRIES) {
        return;
    }
    if (s_match_count >= MAX_MATCHES && score <= s_matches[MAX_MATCHES - 1].score) {
        return;
    }

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
        int dyn_idx = s_dyn_count++;
        wcsncpy(s_dynamic_entries[dyn_idx].name, name, 127);
        s_dynamic_entries[dyn_idx].name[127] = L'\0';
        wcsncpy(s_dynamic_entries[dyn_idx].target, target, ALIAS_TARGET_LEN - 1);
        s_dynamic_entries[dyn_idx].target[ALIAS_TARGET_LEN - 1] = L'\0';
        wcsncpy(s_dynamic_entries[dyn_idx].desc, desc ? desc : L"", 127);
        s_dynamic_entries[dyn_idx].desc[127] = L'\0';
        s_dynamic_entries[dyn_idx].is_action = true;
        s_dynamic_entries[dyn_idx].hicon = resolve_icon(target, true);

        int limit = s_match_count < MAX_MATCHES ? s_match_count : (MAX_MATCHES - 1);
        for (int k = limit; k > insert_pos; k--) {
            s_matches[k] = s_matches[k - 1];
        }
        s_matches[insert_pos].app_index = -(dyn_idx + 1);
        s_matches[insert_pos].score = score;
        if (s_match_count < MAX_MATCHES) {
            s_match_count++;
        }
    }
}

static void suggest_alias_targets(const wchar_t *alias_name, const wchar_t *cible)
{
    wchar_t clean_alias[64];
    wcsncpy(clean_alias, alias_name ? alias_name : L"", 63);
    clean_alias[63] = L'\0';
    unquote_str(clean_alias);
    const wchar_t *disp_alias = (clean_alias[0] != L'\0') ? clean_alias : L"<nom>";

    // If cible is empty, propose popular/installed apps
    if (cible == NULL || *cible == L'\0') {
        int count = 0;
        // Pass 1: popular apps
        for (int i = 0; i < s_app_count && count < 5; i++) {
            if (s_apps[i].is_action) continue;
            if (wcsncmp(s_apps[i].desc, L"Alias", 5) == 0) continue;
            if (!is_popular_app(s_apps[i].name)) continue;

            wchar_t title[128];
            _snwprintf(title, sizeof(title)/sizeof(title[0]), L"%s \u2794 %s", disp_alias, s_apps[i].name);

            bool dup = false;
            for (int d = 0; d < s_dyn_count; d++) {
                if (wcscmp(s_dynamic_entries[d].name, title) == 0) {
                    dup = true;
                    break;
                }
            }
            if (dup) continue;

            const wchar_t *fname = wcsrchr(s_apps[i].target, L'\\');
            fname = (fname != NULL) ? (fname + 1) : s_apps[i].target;

            wchar_t action_target[ALIAS_TARGET_LEN];
            _snwprintf(action_target, sizeof(action_target)/sizeof(action_target[0]), L"__alias_add:%s:%s", disp_alias, s_apps[i].target);
            wchar_t desc[128];
            _snwprintf(desc, sizeof(desc)/sizeof(desc[0]), L"%s", fname);
            add_dynamic_match(title, action_target, desc, 4800 - count * 10);
            count++;
        }

        // Pass 2: any other apps if we need more
        for (int i = 0; i < s_app_count && count < 5; i++) {
            if (s_apps[i].is_action) continue;
            if (wcsncmp(s_apps[i].desc, L"Alias", 5) == 0) continue;

            wchar_t title[128];
            _snwprintf(title, sizeof(title)/sizeof(title[0]), L"%s \u2794 %s", disp_alias, s_apps[i].name);

            bool dup = false;
            for (int d = 0; d < s_dyn_count; d++) {
                if (wcscmp(s_dynamic_entries[d].name, title) == 0) {
                    dup = true;
                    break;
                }
            }
            if (dup) continue;

            const wchar_t *fname = wcsrchr(s_apps[i].target, L'\\');
            fname = (fname != NULL) ? (fname + 1) : s_apps[i].target;

            wchar_t action_target[ALIAS_TARGET_LEN];
            _snwprintf(action_target, sizeof(action_target)/sizeof(action_target[0]), L"__alias_add:%s:%s", disp_alias, s_apps[i].target);
            wchar_t desc[128];
            _snwprintf(desc, sizeof(desc)/sizeof(desc[0]), L"%s", fname);
            add_dynamic_match(title, action_target, desc, 4800 - count * 10);
            count++;
        }
        return;
    }

    // cible is non-empty
    wchar_t clean_cible[ALIAS_TARGET_LEN];
    wcsncpy(clean_cible, cible, ALIAS_TARGET_LEN - 1);
    clean_cible[ALIAS_TARGET_LEN - 1] = L'\0';
    unquote_str(clean_cible);

    // 1. Is it a URL?
    bool is_url = (wcs_starts_with_ci(clean_cible, L"http://") ||
                   wcs_starts_with_ci(clean_cible, L"https://") ||
                   wcs_starts_with_ci(clean_cible, L"www."));
    if (is_url) {
        wchar_t title[128];
        _snwprintf(title, sizeof(title)/sizeof(title[0]), L"%s \u2794 %s", disp_alias, clean_cible);
        wchar_t action_target[ALIAS_TARGET_LEN];
        _snwprintf(action_target, sizeof(action_target)/sizeof(action_target[0]), L"__alias_add:%s:%s", disp_alias, clean_cible);
        wchar_t desc[128];
        _snwprintf(desc, sizeof(desc)/sizeof(desc[0]), L"Lien web (supporte %%s pour la recherche)");
        add_dynamic_match(title, action_target, desc, 5000);
    }

    // 2. Is it a file or directory path?
    bool is_path = (GetFileAttributesW(clean_cible) != INVALID_FILE_ATTRIBUTES ||
                    wcschr(clean_cible, L'\\') != NULL ||
                    wcschr(clean_cible, L'/') != NULL);
    if (is_path && !is_url) {
        wchar_t title[128];
        _snwprintf(title, sizeof(title)/sizeof(title[0]), L"%s \u2794 %s", disp_alias, clean_cible);
        wchar_t action_target[ALIAS_TARGET_LEN];
        _snwprintf(action_target, sizeof(action_target)/sizeof(action_target[0]), L"__alias_add:%s:%s", disp_alias, clean_cible);
        wchar_t desc[128];
        _snwprintf(desc, sizeof(desc)/sizeof(desc[0]), L"Fichier ou dossier local");
        add_dynamic_match(title, action_target, desc, 4900);
    }

    // 3. Search indexed applications matching clean_cible
    size_t clen = wcslen(clean_cible);
    for (int i = 0; i < s_app_count; i++) {
        if (s_apps[i].is_action) continue;
        if (wcsncmp(s_apps[i].desc, L"Alias", 5) == 0) continue;

        const wchar_t *app_name = s_apps[i].name;
        const wchar_t *app_target = s_apps[i].target;
        size_t nlen = wcslen(app_name);

        const wchar_t *fname = wcsrchr(app_target, L'\\');
        if (fname != NULL) {
            fname++;
        } else {
            fname = app_target;
        }

        int score = 0;

        // Exact match
        if (_wcsicmp(app_name, clean_cible) == 0) {
            score = 4850;
        } else if (_wcsicmp(fname, clean_cible) == 0) {
            score = 4840;
        } else if (wcs_ends_with_ci(fname, L".exe") &&
                   _wcsnicmp(fname, clean_cible, clen) == 0 &&
                   fname[clen] == L'.') {
            score = 4840;
        }
        // Prefix match on app_name
        else if (wcs_starts_with_ci(app_name, clean_cible)) {
            score = 4750 - (int)(nlen - clen);
        }
        // Prefix match on fname
        else if (wcs_starts_with_ci(fname, clean_cible)) {
            score = 4700 - (int)(wcslen(fname) - clen);
        }
        // Word boundary match in app_name
        else {
            const wchar_t *p = app_name;
            while (*p != L'\0') {
                while (*p == L' ' || *p == L'-' || *p == L'_') p++;
                if (wcs_starts_with_ci(p, clean_cible)) {
                    score = 4650 - (int)(nlen - clen);
                    break;
                }
                while (*p != L'\0' && *p != L' ' && *p != L'-' && *p != L'_') p++;
            }

            // Substring in app_name
            if (score == 0 && wcs_contains_ci(app_name, clean_cible)) {
                score = 4500 - (int)(nlen - clen);
            }
            // Substring in fname or target
            else if (score == 0 && (wcs_contains_ci(fname, clean_cible) || wcs_contains_ci(app_target, clean_cible))) {
                score = 4400;
            }
            // Fuzzy match on app_name or fname
            else if (score == 0) {
                int fz = compute_fuzzy_score(clean_cible, app_name);
                if (fz > 0) {
                    score = 4100 + (fz - 700) / 2;
                } else {
                    int fz_fn = compute_fuzzy_score(clean_cible, fname);
                    if (fz_fn > 0) {
                        score = 4050 + (fz_fn - 700) / 2;
                    }
                }
            }
        }

        if (score > 0) {
            wchar_t title[128];
            _snwprintf(title, sizeof(title)/sizeof(title[0]), L"%s \u2794 %s", disp_alias, app_name);

            bool dup = false;
            for (int d = 0; d < s_dyn_count; d++) {
                if (wcscmp(s_dynamic_entries[d].name, title) == 0) {
                    dup = true;
                    break;
                }
            }
            if (dup) continue;

            wchar_t action_target[ALIAS_TARGET_LEN];
            _snwprintf(action_target, sizeof(action_target)/sizeof(action_target[0]), L"__alias_add:%s:%s", disp_alias, app_target);
            wchar_t desc[128];
            _snwprintf(desc, sizeof(desc)/sizeof(desc[0]), L"%s", fname);
            add_dynamic_match(title, action_target, desc, score);
        }
    }

    // 4. Literal command fallback (if not already added as URL or path)
    if (!is_url && !is_path) {
        wchar_t title[128];
        _snwprintf(title, sizeof(title)/sizeof(title[0]), L"Ajouter l'alias \"%s\" \u2794 %s", disp_alias, clean_cible);
        wchar_t action_target[ALIAS_TARGET_LEN];
        _snwprintf(action_target, sizeof(action_target)/sizeof(action_target[0]), L"__alias_add:%s:%s", disp_alias, clean_cible);
        wchar_t desc[128];
        _snwprintf(desc, sizeof(desc)/sizeof(desc[0]), L"Commande brute dans klypr.ini");
        add_dynamic_match(title, action_target, desc, 4000);
    }
}

static void filter_apps(const wchar_t *query)
{
    s_match_count = 0;
    s_selected_index = 0;
    for (int i = 0; i < s_dyn_count; i++) {
        if (s_dynamic_entries[i].hicon != NULL) {
            DestroyIcon(s_dynamic_entries[i].hicon);
            s_dynamic_entries[i].hicon = NULL;
        }
    }
    s_dyn_count = 0;

    if (query == NULL) {
        return;
    }

    while (*query == L' ' || *query == L'\t') {
        query++;
    }

    if (*query == L'\0') {
        return;
    }

    // 1. Check for Alias management commands (:alias or alias)
    bool is_alias_exact = (_wcsicmp(query, L":alias") == 0 || _wcsicmp(query, L"alias") == 0);
    bool is_alias_cmd = (wcs_starts_with_ci(query, L":alias ") || wcs_starts_with_ci(query, L"alias "));

    if (is_alias_exact) {
        add_dynamic_match(
            L"Configuration: \u00c9diter les alias (klypr.ini)",
            L"__config:open",
            L"Ouvrir klypr.ini dans le Bloc-notes",
            5000
        );
        add_dynamic_match(
            L"Ajouter un alias : :alias <nom> <cible>",
            L"__config:open",
            L"Ex: :alias g https://google.com/search?q=%s",
            4900
        );
        add_dynamic_match(
            L"Supprimer un alias : :alias del <nom>",
            L"__config:open",
            L"Ex: :alias del g",
            4800
        );

        // Also list existing aliases
        for (int k = 0; k < g_config.alias_count && s_match_count < MAX_MATCHES; k++) {
            wchar_t title[128];
            _snwprintf(title, sizeof(title)/sizeof(title[0]), L"Alias \"%s\"", g_config.aliases[k].name);
            wchar_t desc[128];
            _snwprintf(desc, sizeof(desc)/sizeof(desc[0]), L"\u2794 %s", g_config.aliases[k].target);
            add_dynamic_match(title, g_config.aliases[k].target, desc, 4700 - k);
        }
        return;
    } else if (is_alias_cmd) {
        bool has_colon = (query[0] == L':');
        const wchar_t *cmd_word = has_colon ? L":alias" : L"alias";
        const wchar_t *cmd_args = query + (has_colon ? 7 : 6);
        while (*cmd_args == L' ' || *cmd_args == L'\t') cmd_args++;

        if (*cmd_args == L'\0') {
            wchar_t title[128];
            _snwprintf(title, sizeof(title)/sizeof(title[0]), L"%s <nom> <cible>", cmd_word);
            add_dynamic_match(
                title,
                L"__config:open",
                L"Tapez le nom de l'alias suivi de la cible",
                5000
            );
            suggest_alias_targets(L"<nom>", L"");
            return;
        }

        if (wcs_starts_with_ci(cmd_args, L"del ") || wcs_starts_with_ci(cmd_args, L"rm ") || wcs_starts_with_ci(cmd_args, L"remove ")) {
            const wchar_t *name = cmd_args + (wcs_starts_with_ci(cmd_args, L"remove ") ? 7 : (wcs_starts_with_ci(cmd_args, L"del ") ? 4 : 3));
            while (*name == L' ' || *name == L'\t') name++;

            if (*name == L'\0') {
                wchar_t title[128];
                _snwprintf(title, sizeof(title)/sizeof(title[0]), L"%s del <nom>", cmd_word);
                add_dynamic_match(
                    title,
                    L"__config:open",
                    L"S\u00e9lectionnez un alias ci-dessous ou tapez son nom",
                    5000
                );
            }

            for (int k = 0; k < g_config.alias_count; k++) {
                if (*name == L'\0' || wcs_contains_ci(g_config.aliases[k].name, name)) {
                    wchar_t title[128];
                    _snwprintf(title, sizeof(title)/sizeof(title[0]), L"Supprimer l'alias \"%s\"", g_config.aliases[k].name);
                    wchar_t action_target[ALIAS_TARGET_LEN];
                    _snwprintf(action_target, sizeof(action_target)/sizeof(action_target[0]), L"__alias_del:%s", g_config.aliases[k].name);
                    wchar_t desc[128];
                    _snwprintf(desc, sizeof(desc)/sizeof(desc[0]), L"Cible : %s", g_config.aliases[k].target);
                    add_dynamic_match(title, action_target, desc, 4800 - k);
                }
            }

            if (*name != L'\0') {
                wchar_t title[128];
                _snwprintf(title, sizeof(title)/sizeof(title[0]), L"Supprimer l'alias \"%s\"", name);
                wchar_t action_target[ALIAS_TARGET_LEN];
                _snwprintf(action_target, sizeof(action_target)/sizeof(action_target[0]), L"__alias_del:%s", name);
                add_dynamic_match(title, action_target, L"Supprimer de klypr.ini", 4000);
            }
            return;
        }

        const wchar_t *p = cmd_args;
        if (wcs_starts_with_ci(p, L"add ")) {
            p += 4;
            while (*p == L' ' || *p == L'\t') p++;
        }

        const wchar_t *space = wcschr(p, L' ');
        const wchar_t *eq = wcschr(p, L'=');
        const wchar_t *delim = space;
        if (eq != NULL && (delim == NULL || eq < delim)) {
            delim = eq;
        }

        if (delim == NULL) {
            wchar_t alias_name[64];
            wcsncpy(alias_name, p, 63);
            alias_name[63] = L'\0';
            unquote_str(alias_name);

            wchar_t title[128];
            _snwprintf(title, sizeof(title)/sizeof(title[0]), L"%s %s <cible>", cmd_word, alias_name);
            wchar_t desc[128];
            _snwprintf(desc, sizeof(desc)/sizeof(desc[0]), L"Tapez un espace puis la cible (ou choisissez ci-dessous)");
            add_dynamic_match(title, L"__config:open", desc, 5000);

            suggest_alias_targets(alias_name, L"");
        } else {
            wchar_t alias_name[64];
            size_t nlen = (size_t)(delim - p);
            if (nlen >= 64) nlen = 63;
            wcsncpy(alias_name, p, nlen);
            alias_name[nlen] = L'\0';

            while (nlen > 0 && (alias_name[nlen - 1] == L' ' || alias_name[nlen - 1] == L'\t')) {
                alias_name[--nlen] = L'\0';
            }
            unquote_str(alias_name);

            const wchar_t *cible = delim + 1;
            while (*cible == L' ' || *cible == L'\t' || *cible == L'=') cible++;

            if (*cible == L'\0') {
                wchar_t title[128];
                _snwprintf(title, sizeof(title)/sizeof(title[0]), L"%s %s <cible>", cmd_word, alias_name);
                wchar_t desc[128];
                _snwprintf(desc, sizeof(desc)/sizeof(desc[0]), L"Tapez la cible (app, URL avec %%s, dossier...)");
                add_dynamic_match(title, L"__config:open", desc, 5000);
            }

            suggest_alias_targets(alias_name, cible);
        }
        return;
    }

    // 2. Check for Alias invocation with parameters (e.g. "g query", "gh repo", "code project")
    const wchar_t *first_space = wcschr(query, L' ');
    if (first_space != NULL) {
        wchar_t alias_cand[64];
        size_t clen = (size_t)(first_space - query);
        if (clen < 64) {
            wcsncpy(alias_cand, query, clen);
            alias_cand[clen] = L'\0';

            const wchar_t *param_str = first_space + 1;
            while (*param_str == L' ' || *param_str == L'\t') param_str++;

            if (*param_str != L'\0') {
                const wchar_t *alias_target = config_get_alias_target(alias_cand);
                if (alias_target != NULL) {
                    wchar_t resolved_target[ALIAS_TARGET_LEN];
                    wchar_t resolved_desc[128];

                    if (wcsstr(alias_target, L"%s") != NULL) {
                        wchar_t encoded[ALIAS_TARGET_LEN];
                        if (wcs_starts_with_ci(alias_target, L"http://") || wcs_starts_with_ci(alias_target, L"https://")) {
                            url_encode(param_str, encoded, ALIAS_TARGET_LEN);
                        } else {
                            wcsncpy(encoded, param_str, ALIAS_TARGET_LEN - 1);
                            encoded[ALIAS_TARGET_LEN - 1] = L'\0';
                        }
                        const wchar_t *sub = wcsstr(alias_target, L"%s");
                        size_t pre_len = (size_t)(sub - alias_target);
                        _snwprintf(resolved_target, ALIAS_TARGET_LEN, L"%.*s%s%s", (int)pre_len, alias_target, encoded, sub + 2);
                        _snwprintf(resolved_desc, sizeof(resolved_desc)/sizeof(resolved_desc[0]), L"Alias \"%s\" avec param\u00e8tres", alias_cand);
                    } else if (wcs_starts_with_ci(alias_target, L"http://") || wcs_starts_with_ci(alias_target, L"https://")) {
                        wchar_t encoded[ALIAS_TARGET_LEN];
                        url_encode(param_str, encoded, ALIAS_TARGET_LEN);
                        size_t t_len = wcslen(alias_target);
                        if (alias_target[t_len - 1] == L'=' || alias_target[t_len - 1] == L'/') {
                            _snwprintf(resolved_target, ALIAS_TARGET_LEN, L"%s%s", alias_target, encoded);
                        } else {
                            _snwprintf(resolved_target, ALIAS_TARGET_LEN, L"%s?q=%s", alias_target, encoded);
                        }
                        _snwprintf(resolved_desc, sizeof(resolved_desc)/sizeof(resolved_desc[0]), L"Recherche web via \"%s\"", alias_cand);
                    } else {
                        _snwprintf(resolved_target, ALIAS_TARGET_LEN, L"%s %s", alias_target, param_str);
                        _snwprintf(resolved_desc, sizeof(resolved_desc)/sizeof(resolved_desc[0]), L"Ex\u00e9cuter avec l'alias \"%s\"", alias_cand);
                    }

                    add_dynamic_match(query, resolved_target, resolved_desc, 3000);
                }
            }
        }
    }

    // 3. Normal search in indexed apps, actions and aliases
    size_t qlen = wcslen(query);
    for (int i = 0; i < s_app_count; i++) {
        int score = 0;
        const wchar_t *name = s_apps[i].name;
        size_t nlen = wcslen(name);
        bool is_alias = (wcsncmp(s_apps[i].desc, L"Alias", 5) == 0);

        if (s_apps[i].is_action) {
            if (_wcsicmp(name, query) == 0) {
                score = 2500;
            } else if (wcs_starts_with_ci(name, query)) {
                score = 1500 - (int)(nlen - qlen);
            } else if (wcs_contains_ci(name, query) || wcs_contains_ci(s_apps[i].desc, query)) {
                score = 1100 - (int)(nlen - qlen);
            } else {
                int fz = compute_fuzzy_score(query, name);
                if (fz > 0) {
                    score = fz;
                }
            }
        } else if (is_alias) {
            if (_wcsicmp(name, query) == 0) {
                score = 3000;
            } else if (wcs_starts_with_ci(name, query)) {
                score = 2400 - (int)(nlen - qlen) * 2;
            } else if (wcs_contains_ci(name, query)) {
                score = 1600 - (int)(nlen - qlen) * 2;
            } else if (wcs_contains_ci(s_apps[i].target, query)) {
                score = 1200;
            } else {
                int fz = compute_fuzzy_score(query, name);
                if (fz > 0) {
                    score = 1000 + (fz - 700) / 2;
                }
            }
        } else {
            const wchar_t *fname = wcsrchr(s_apps[i].target, L'\\');
            fname = (fname != NULL) ? (fname + 1) : s_apps[i].target;
            size_t flen = wcslen(fname);

            // 1. Exact match on name or fname (or fname without .exe)
            if (_wcsicmp(name, query) == 0) {
                score = 2800;
            } else if (_wcsicmp(fname, query) == 0) {
                score = 2750;
            } else if (wcs_ends_with_ci(fname, L".exe") &&
                       _wcsnicmp(fname, query, qlen) == 0 &&
                       fname[qlen] == L'.') {
                score = 2750;
            }
            // 2. Starts with on name
            else if (wcs_starts_with_ci(name, query)) {
                score = 2200 - (int)(nlen - qlen) * 2;
            }
            // 3. Starts with on fname (e.g. wt.exe, chrome.exe)
            else if (wcs_starts_with_ci(fname, query)) {
                score = 2000 - (int)(flen - qlen) * 2;
            }
            // 4. Word boundary match on name (e.g. "Google Chrome" matching "Chrome")
            else {
                const wchar_t *p = name;
                while (*p != L'\0') {
                    while (*p == L' ' || *p == L'-' || *p == L'_') {
                        p++;
                    }
                    if (wcs_starts_with_ci(p, query)) {
                        score = 1700 - (int)(nlen - qlen) * 2;
                        break;
                    }
                    while (*p != L'\0' && *p != L' ' && *p != L'-' && *p != L'_') {
                        p++;
                    }
                }

                // 5. Substring match in name
                if (score == 0 && wcs_contains_ci(name, query)) {
                    score = 1300 - (int)(nlen - qlen) * 2;
                }
                // 6. Substring match in target or fname
                else if (score == 0 && (wcs_contains_ci(fname, query) || wcs_contains_ci(s_apps[i].target, query))) {
                    score = 1000;
                }
                // 7. Fuzzy match on name
                else if (score == 0) {
                    int fz = compute_fuzzy_score(query, name);
                    if (fz > 0) {
                        score = fz;
                    } else {
                        // 8. Fuzzy match on fname
                        int fz_fn = compute_fuzzy_score(query, fname);
                        if (fz_fn > 0) {
                            score = fz_fn - 50;
                        }
                    }
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

    // 4. URL fallback if no matches
    if (s_match_count == 0) {
        wchar_t normalized_url[MAX_PATH];
        if (is_likely_url(query, normalized_url, MAX_PATH)) {
            add_dynamic_match(normalized_url, normalized_url, L"Navigateur Web", 1000);
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

    // Check if this is an internal config open action
    if (wcscmp(input, L"__config:open") == 0) {
        ShellExecuteW(NULL, L"open", config_get_ini_path(), NULL, NULL, SW_SHOWNORMAL);
        return;
    }

    // Check if this is an internal alias add action: __alias_add:<name>:<target>
    if (wcsncmp(input, L"__alias_add:", 12) == 0) {
        const wchar_t *payload = input + 12;
        const wchar_t *sep = wcschr(payload, L':');
        if (sep != NULL) {
            wchar_t alias_name[64];
            size_t nlen = (size_t)(sep - payload);
            if (nlen >= 64) nlen = 63;
            wcsncpy(alias_name, payload, nlen);
            alias_name[nlen] = L'\0';

            const wchar_t *target = sep + 1;
            config_add_alias(alias_name, target);
            index_all_applications();
        }
        return;
    }

    // Check if this is an internal alias delete action: __alias_del:<name>
    if (wcsncmp(input, L"__alias_del:", 12) == 0) {
        const wchar_t *alias_name = input + 12;
        config_remove_alias(alias_name);
        index_all_applications();
        return;
    }

    // Check if this is an internal theme switch action
    if (wcsncmp(input, L"__theme:", 8) == 0) {
        int theme_id = _wtoi(input + 8);
        config_set_theme((ThemeType)theme_id);
        launcher_apply_theme();
        return;
    }

    // Check if this is an internal autostart switch action
    if (wcsncmp(input, L"__autostart:", 12) == 0) {
        bool enable = (input[12] == L'1');
        config_set_autostart(enable);
        index_all_applications();
        return;
    }

    // Direct check if input is an existing file or directory path (supports unquoted paths with spaces)
    if (GetFileAttributesW(input) != INVALID_FILE_ATTRIBUTES) {
        ShellExecuteW(NULL, L"open", input, NULL, NULL, SW_SHOWNORMAL);
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
                const AppEntry *entry = get_entry_for_match(s_selected_index);
                if (entry != NULL) {
                    if (wcsncmp(entry->target, L"__alias_add:", 12) == 0) {
                        const wchar_t *payload = entry->target + 12;
                        const wchar_t *sep = wcschr(payload, L':');
                        if (sep != NULL) {
                            wchar_t a_name[64];
                            size_t nlen = (size_t)(sep - payload);
                            if (nlen >= 64) nlen = 63;
                            wcsncpy(a_name, payload, nlen);
                            a_name[nlen] = L'\0';
                            const wchar_t *a_target = sep + 1;

                            wchar_t cur_text[16] = {0};
                            GetWindowTextW(hwnd, cur_text, 15);
                            const wchar_t *cmd_word = (cur_text[0] == L':') ? L":alias" : L"alias";

                            wchar_t tab_buf[ALIAS_TARGET_LEN + 128];
                            _snwprintf(tab_buf, sizeof(tab_buf)/sizeof(tab_buf[0]), L"%s %s %s", cmd_word, a_name, a_target);
                            SetWindowTextW(hwnd, tab_buf);
                            int len = (int)wcslen(tab_buf);
                            SendMessageW(hwnd, EM_SETSEL, len, len);
                            return 0;
                        }
                    } else if (wcsncmp(entry->target, L"__alias_del:", 12) == 0) {
                        const wchar_t *a_name = entry->target + 12;
                        wchar_t cur_text[16] = {0};
                        GetWindowTextW(hwnd, cur_text, 15);
                        const wchar_t *cmd_word = (cur_text[0] == L':') ? L":alias" : L"alias";

                        wchar_t tab_buf[128];
                        _snwprintf(tab_buf, sizeof(tab_buf)/sizeof(tab_buf[0]), L"%s del %s", cmd_word, a_name);
                        SetWindowTextW(hwnd, tab_buf);
                        int len = (int)wcslen(tab_buf);
                        SendMessageW(hwnd, EM_SETSEL, len, len);
                        return 0;
                    } else if (wcscmp(entry->target, L"__config:open") == 0) {
                        int cur_len = GetWindowTextLengthW(hwnd);
                        wchar_t cur_buf[256];
                        GetWindowTextW(hwnd, cur_buf, 255);
                        if (cur_len > 0 && cur_buf[cur_len - 1] != L' ') {
                            cur_buf[cur_len] = L' ';
                            cur_buf[cur_len + 1] = L'\0';
                            SetWindowTextW(hwnd, cur_buf);
                            SendMessageW(hwnd, EM_SETSEL, cur_len + 1, cur_len + 1);
                        }
                        return 0;
                    }

                    SetWindowTextW(hwnd, entry->name);
                    int len = (int)wcslen(entry->name);
                    SendMessageW(hwnd, EM_SETSEL, len, len);
                }
            }
            return 0;
        }

        if (wParam == VK_RETURN) {
            bool alt_is_down = ((GetKeyState(VK_MENU) & 0x8000) != 0);
            if (alt_is_down) {
                launcher_hide();
                input_launch_terminal();
                return 0;
            }

            wchar_t target_to_exec[ALIAS_TARGET_LEN] = {0};
            if (s_match_count > 0 && s_selected_index >= 0 && s_selected_index < s_match_count) {
                const AppEntry *entry = get_entry_for_match(s_selected_index);
                if (entry != NULL) {
                    wcsncpy(target_to_exec, entry->target, ALIAS_TARGET_LEN - 1);
                }
            } else {
                GetWindowTextW(hwnd, target_to_exec, ALIAS_TARGET_LEN - 1);
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

    case WM_KLYPR_SHOW: {
        launcher_show();
        return 0;
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
        if (y < BASE_HEIGHT) {
            if (s_hwnd_edit != NULL) {
                SetFocus(s_hwnd_edit);
            }
            return 0;
        }
        if (s_match_count > 0) {
            int clicked = (y - BASE_HEIGHT - 4) / ITEM_HEIGHT;
            if (clicked >= 0 && clicked < s_match_count) {
                const AppEntry *entry = get_entry_for_match(clicked);
                if (entry != NULL) {
                    launcher_hide();
                    execute_command(entry->target);
                    return 0;
                }
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

                const AppEntry *entry = get_entry_for_match(i);
                const wchar_t *name = entry ? entry->name : L"";
                const wchar_t *desc = entry ? entry->desc : L"";
                HICON hicon = entry ? entry->hicon : NULL;

                SIZE desc_size;
                GetTextExtentPoint32W(mem_dc, desc, (int)wcslen(desc), &desc_size);

                int text_left = 74;
                int text_right = w - 30 - desc_size.cx;
                if (text_right < 200) {
                    text_right = 200;
                }

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
                    TextOutW(mem_dc, 24, item_top + 9, L"\u279c", 1);

                    // Draw application icon
                    if (hicon != NULL) {
                        DrawIconEx(mem_dc, 44, item_top + 9, hicon, 20, 20, 0, NULL, DI_NORMAL);
                    }

                    // Primary name with auto ellipsis
                    SetTextColor(mem_dc, s_current_theme.text_primary);
                    RECT name_rect = { text_left, item_top + 9, text_right, item_bottom };
                    DrawTextW(mem_dc, (LPWSTR)name, -1, &name_rect, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

                    // Secondary description on right
                    SetTextColor(mem_dc, s_current_theme.accent_color);
                    TextOutW(mem_dc, w - 24 - desc_size.cx, item_top + 9, desc, (int)wcslen(desc));
                } else {
                    // Unselected item bullet
                    SetTextColor(mem_dc, s_current_theme.text_secondary);
                    TextOutW(mem_dc, 26, item_top + 9, L"\u00b7", 1);

                    // Draw application icon
                    if (hicon != NULL) {
                        DrawIconEx(mem_dc, 44, item_top + 9, hicon, 20, 20, 0, NULL, DI_NORMAL);
                    }

                    // Primary name with auto ellipsis
                    SetTextColor(mem_dc, s_current_theme.text_primary);
                    RECT name_rect = { text_left, item_top + 9, text_right, item_bottom };
                    DrawTextW(mem_dc, (LPWSTR)name, -1, &name_rect, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

                    // Secondary description on right
                    SetTextColor(mem_dc, s_current_theme.text_secondary);
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
        } else {
            if (s_hwnd_edit != NULL) {
                SetFocus(s_hwnd_edit);
            }
        }
        return 0;
    }

    case WM_SETFOCUS: {
        if (s_hwnd_edit != NULL) {
            SetFocus(s_hwnd_edit);
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
    for (int i = 0; i < s_app_count; i++) {
        if (s_apps[i].hicon != NULL) {
            DestroyIcon(s_apps[i].hicon);
            s_apps[i].hicon = NULL;
        }
    }
    for (int i = 0; i < s_dyn_count; i++) {
        if (s_dynamic_entries[i].hicon != NULL) {
            DestroyIcon(s_dynamic_entries[i].hicon);
            s_dynamic_entries[i].hicon = NULL;
        }
    }

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

    // Seamlessly attach thread input to guarantee foreground rights & instant keyboard focus
    HWND hwnd_fore = GetForegroundWindow();
    DWORD fore_thread = hwnd_fore ? GetWindowThreadProcessId(hwnd_fore, NULL) : 0;
    DWORD cur_thread = GetCurrentThreadId();

    if (fore_thread != 0 && fore_thread != cur_thread) {
        AttachThreadInput(cur_thread, fore_thread, TRUE);
    }

    ShowWindow(s_hwnd_launcher, SW_SHOW);
    BringWindowToTop(s_hwnd_launcher);
    SetForegroundWindow(s_hwnd_launcher);
    SetActiveWindow(s_hwnd_launcher);
    SetFocus(s_hwnd_edit);

    if (fore_thread != 0 && fore_thread != cur_thread) {
        AttachThreadInput(cur_thread, fore_thread, FALSE);
    }

    SendMessageW(s_hwnd_edit, EM_SETSEL, 0, 0);

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
