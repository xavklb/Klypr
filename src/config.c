#include "config.h"
#include <stdio.h>
#include <wchar.h>

AppConfig g_config = {
    .theme = THEME_SYSTEM,
    .opacity = 95,
    .autostart = true
};

static wchar_t s_ini_path[MAX_PATH] = {0};

static void init_ini_path(void)
{
    if (s_ini_path[0] != L'\0') {
        return;
    }

    GetModuleFileNameW(NULL, s_ini_path, MAX_PATH);
    wchar_t *last_slash = wcsrchr(s_ini_path, L'\\');
    if (last_slash != NULL) {
        *(last_slash + 1) = L'\0';
        wcsncat(s_ini_path, L"klypr.ini", MAX_PATH - wcslen(s_ini_path) - 1);
    } else {
        wcsncpy(s_ini_path, L".\\klypr.ini", MAX_PATH - 1);
    }
}

static bool autostart_sync_registry(bool enable)
{
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS) {
        return false;
    }

    if (enable) {
        wchar_t exe_path[MAX_PATH];
        GetModuleFileNameW(NULL, exe_path, MAX_PATH);

        wchar_t quoted_path[MAX_PATH + 4];
        _snwprintf(quoted_path, sizeof(quoted_path) / sizeof(quoted_path[0]), L"\"%s\"", exe_path);

        RegSetValueExW(hKey, L"Klypr", 0, REG_SZ, (const BYTE *)quoted_path, (DWORD)((wcslen(quoted_path) + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(hKey, L"Klypr");
    }

    RegCloseKey(hKey);
    return true;
}

const wchar_t *config_theme_to_string(ThemeType theme)
{
    switch (theme) {
    case THEME_DARK:
        return L"dark";
    case THEME_LIGHT:
        return L"light";
    case THEME_CYBERPUNK:
        return L"cyberpunk";
    case THEME_DRACULA:
        return L"dracula";
    case THEME_SYSTEM:
    default:
        return L"system";
    }
}

ThemeType config_string_to_theme(const wchar_t *str)
{
    if (str == NULL) {
        return THEME_SYSTEM;
    }

    if (_wcsicmp(str, L"dark") == 0 || _wcsicmp(str, L"sombre") == 0) {
        return THEME_DARK;
    }
    if (_wcsicmp(str, L"light") == 0 || _wcsicmp(str, L"clair") == 0) {
        return THEME_LIGHT;
    }
    if (_wcsicmp(str, L"cyberpunk") == 0 || _wcsicmp(str, L"terminal") == 0 || _wcsicmp(str, L"matrix") == 0) {
        return THEME_CYBERPUNK;
    }
    if (_wcsicmp(str, L"dracula") == 0 || _wcsicmp(str, L"violet") == 0) {
        return THEME_DRACULA;
    }

    return THEME_SYSTEM;
}

bool config_init(void)
{
    init_ini_path();

    g_config.autostart = GetPrivateProfileIntW(L"General", L"autostart", 1, s_ini_path) != 0;
    autostart_sync_registry(g_config.autostart);

    g_config.opacity = GetPrivateProfileIntW(L"Theme", L"opacity", 95, s_ini_path);

    if (g_config.opacity < 50) g_config.opacity = 50;
    if (g_config.opacity > 100) g_config.opacity = 100;

    wchar_t theme_buf[64] = {0};
    GetPrivateProfileStringW(L"Theme", L"theme", L"system", theme_buf, 63, s_ini_path);
    g_config.theme = config_string_to_theme(theme_buf);

    // Save to create ini file if it doesn't exist yet
    config_save();
    return true;
}

void config_save(void)
{
    init_ini_path();

    WritePrivateProfileStringW(L"General", L"autostart", g_config.autostart ? L"1" : L"0", s_ini_path);

    WritePrivateProfileStringW(L"Theme", L"theme", config_theme_to_string(g_config.theme), s_ini_path);

    wchar_t num_buf[32];
    _snwprintf(num_buf, 32, L"%d", g_config.opacity);
    WritePrivateProfileStringW(L"Theme", L"opacity", num_buf, s_ini_path);
}

void config_set_theme(ThemeType theme)
{
    if (theme >= THEME_COUNT) {
        theme = THEME_SYSTEM;
    }
    g_config.theme = theme;
    config_save();
}

void config_set_autostart(bool enabled)
{
    g_config.autostart = enabled;
    autostart_sync_registry(enabled);
    config_save();
}

void config_cleanup(void)
{
}
