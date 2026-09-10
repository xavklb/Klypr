#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include <windows.h>

#define MAX_ALIASES 128
#define ALIAS_TARGET_LEN 512

typedef enum {
    THEME_SYSTEM = 0,
    THEME_DARK,
    THEME_LIGHT,
    THEME_CYBERPUNK,
    THEME_DRACULA,
    THEME_COUNT
} ThemeType;

typedef struct {
    wchar_t name[64];
    wchar_t target[ALIAS_TARGET_LEN];
} AliasEntry;

typedef struct {
    ThemeType theme;
    int opacity;
    bool autostart;
    wchar_t terminal[MAX_PATH];
    wchar_t search_engine[ALIAS_TARGET_LEN];
    AliasEntry aliases[MAX_ALIASES];
    int alias_count;
} AppConfig;

extern AppConfig g_config;

bool config_init(void);
void config_cleanup(void);
void config_set_theme(ThemeType theme);
void config_set_autostart(bool enabled);
void config_set_terminal(const wchar_t *terminal);
void config_set_search_engine(const wchar_t *engine);
void config_save(void);
const wchar_t *config_theme_to_string(ThemeType theme);
ThemeType config_string_to_theme(const wchar_t *str);

// Alias API
void config_load_aliases(void);
bool config_add_alias(const wchar_t *name, const wchar_t *target);
bool config_remove_alias(const wchar_t *name);
const wchar_t *config_get_alias_target(const wchar_t *name);
const wchar_t *config_get_ini_path(void);

#endif
