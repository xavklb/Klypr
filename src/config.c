#include "config.h"
#include <stdio.h>
#include <wchar.h>

AppConfig g_config = {
    .theme = THEME_SYSTEM,
    .opacity = 95,
    .autostart = true,
    .terminal = L"wt.exe",
    .search_engine = L"https://www.google.com/search?q=%s"
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

    GetPrivateProfileStringW(L"General", L"terminal", L"wt.exe", g_config.terminal, MAX_PATH - 1, s_ini_path);
    GetPrivateProfileStringW(L"General", L"search_engine", L"https://www.google.com/search?q=%s", g_config.search_engine, ALIAS_TARGET_LEN - 1, s_ini_path);

    wchar_t theme_buf[64] = {0};
    GetPrivateProfileStringW(L"Theme", L"theme", L"system", theme_buf, 63, s_ini_path);
    g_config.theme = config_string_to_theme(theme_buf);

    config_load_aliases();

    // Default aliases if none are configured yet
    if (g_config.alias_count == 0) {
        config_add_alias(L"g", L"https://www.google.com/search?q=%s");
        config_add_alias(L"yt", L"https://www.youtube.com/results?search_query=%s");
        config_add_alias(L"gh", L"https://github.com/search?q=%s");
        config_add_alias(L"term", L"wt.exe");
    }

    // Save to create ini file if it doesn't exist yet
    config_save();
    return true;
}

void config_save(void)
{
    init_ini_path();

    WritePrivateProfileStringW(L"General", L"autostart", g_config.autostart ? L"1" : L"0", s_ini_path);
    WritePrivateProfileStringW(L"General", L"terminal", g_config.terminal, s_ini_path);
    WritePrivateProfileStringW(L"General", L"search_engine", g_config.search_engine, s_ini_path);

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

void config_set_terminal(const wchar_t *terminal)
{
    if (terminal != NULL) {
        wcsncpy(g_config.terminal, terminal, MAX_PATH - 1);
        g_config.terminal[MAX_PATH - 1] = L'\0';
        config_save();
    }
}

void config_set_search_engine(const wchar_t *engine)
{
    if (engine != NULL && engine[0] != L'\0') {
        wcsncpy(g_config.search_engine, engine, ALIAS_TARGET_LEN - 1);
        g_config.search_engine[ALIAS_TARGET_LEN - 1] = L'\0';
        config_save();
    }
}

void config_load_aliases(void)
{
    init_ini_path();
    g_config.alias_count = 0;

    wchar_t buffer[8192];
    DWORD len = GetPrivateProfileSectionW(L"Aliases", buffer, sizeof(buffer) / sizeof(buffer[0]), s_ini_path);
    if (len == 0) {
        return;
    }

    const wchar_t *ptr = buffer;
    while (*ptr != L'\0' && g_config.alias_count < MAX_ALIASES) {
        const wchar_t *eq = wcschr(ptr, L'=');
        if (eq != NULL) {
            const wchar_t *k_start = ptr;
            while (*k_start == L' ' || *k_start == L'\t') k_start++;
            const wchar_t *k_end = eq - 1;
            while (k_end >= k_start && (*k_end == L' ' || *k_end == L'\t')) k_end--;

            const wchar_t *v_start = eq + 1;
            while (*v_start == L' ' || *v_start == L'\t') v_start++;
            const wchar_t *v_end = ptr + wcslen(ptr) - 1;
            while (v_end >= v_start && (*v_end == L' ' || *v_end == L'\t' || *v_end == L'\r' || *v_end == L'\n')) v_end--;

            if (k_end >= k_start && v_end >= v_start) {
                size_t k_len = (size_t)(k_end - k_start + 1);
                size_t v_len = (size_t)(v_end - v_start + 1);

                if (k_len >= 64) k_len = 63;
                if (v_len >= ALIAS_TARGET_LEN) v_len = ALIAS_TARGET_LEN - 1;

                wcsncpy(g_config.aliases[g_config.alias_count].name, k_start, k_len);
                g_config.aliases[g_config.alias_count].name[k_len] = L'\0';

                wcsncpy(g_config.aliases[g_config.alias_count].target, v_start, v_len);
                g_config.aliases[g_config.alias_count].target[v_len] = L'\0';

                g_config.alias_count++;
            }
        }
        ptr += wcslen(ptr) + 1;
    }
}

bool config_add_alias(const wchar_t *name, const wchar_t *target)
{
    if (name == NULL || target == NULL || name[0] == L'\0' || target[0] == L'\0') {
        return false;
    }
    init_ini_path();
    WritePrivateProfileStringW(L"Aliases", name, target, s_ini_path);
    config_load_aliases();
    return true;
}

bool config_remove_alias(const wchar_t *name)
{
    if (name == NULL || name[0] == L'\0') {
        return false;
    }
    init_ini_path();
    WritePrivateProfileStringW(L"Aliases", name, NULL, s_ini_path);
    config_load_aliases();
    return true;
}

const wchar_t *config_get_alias_target(const wchar_t *name)
{
    if (name == NULL || name[0] == L'\0') {
        return NULL;
    }
    for (int i = 0; i < g_config.alias_count; i++) {
        if (_wcsicmp(g_config.aliases[i].name, name) == 0) {
            return g_config.aliases[i].target;
        }
    }
    return NULL;
}

const wchar_t *config_get_ini_path(void)
{
    init_ini_path();
    return s_ini_path;
}

void config_cleanup(void)
{
}
