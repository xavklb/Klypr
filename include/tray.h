#ifndef TRAY_H
#define TRAY_H

#include <stdbool.h>
#include <windows.h>

#define WM_TRAY_NOTIFY (WM_USER + 43)

/**
 * Initializes the Windows System Tray icon using the specified notification window.
 */
bool tray_init(HWND hwnd_notify);

/**
 * Cleans up and removes the system tray icon from the notification area.
 */
void tray_cleanup(void);

/**
 * Handles tray icon messages (clicks, context menu).
 */
void tray_handle_message(HWND hwnd, WPARAM wParam, LPARAM lParam);

/**
 * Re-adds the tray icon (e.g. when Explorer restarts).
 */
void tray_refresh(void);

/**
 * Returns the message ID for TaskbarCreated.
 */
UINT tray_get_taskbar_restart_msg(void);

#endif
