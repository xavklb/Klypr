#ifndef LAUNCHER_H
#define LAUNCHER_H

#include <stdbool.h>
#include <windows.h>

bool launcher_init(HINSTANCE hInstance);
void launcher_cleanup(void);
void launcher_show(void);
void launcher_hide(void);
void launcher_toggle(void);
bool launcher_is_visible(void);
HWND launcher_get_hwnd(void);
void launcher_apply_theme(void);

#define WM_KLYPR_SHOW (WM_USER + 42)

#endif
