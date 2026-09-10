#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include <windows.h>

typedef enum {
    THEME_SYSTEM = 0,
    THEME_DARK,
    THEME_LIGHT,
    THEME_CYBERPUNK,
    THEME_DRACULA,
    THEME_COUNT
} ThemeType;

typedef struct {
    ThemeType theme;
    int opacity;
} AppConfig;

extern AppConfig g_config;

bool config_init(void);
void config_cleanup(void);
void config_set_theme(ThemeType theme);
void config_save(void);
const wchar_t *config_theme_to_string(ThemeType theme);
ThemeType config_string_to_theme(const wchar_t *str);

#endif
