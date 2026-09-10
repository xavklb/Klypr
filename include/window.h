#ifndef WINDOW_H
#define WINDOW_H

#include <windows.h>
#include <stdbool.h>

bool window_is_manageable(HWND hwnd);
void window_scan_manageable(void);

#endif
