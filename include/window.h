#ifndef WINDOW_H
#define WINDOW_H

#include <windows.h>
#include <stdbool.h>

#define WORKSPACE_COUNT 9

bool window_init(void);
void window_cleanup(void);
bool window_is_manageable(HWND hwnd);
void window_scan_manageable(void);
void window_add(HWND hwnd);
void window_remove(HWND hwnd);
void window_arrange_active(void);
void workspace_switch(int index);
int workspace_get_active(void);
bool workspace_contains_window(int ws_index, HWND hwnd);
void workspace_set_focused(HWND hwnd);
void window_focus_under_cursor(POINT pt);
void window_close_active(void);

#endif
