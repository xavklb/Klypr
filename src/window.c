#include "window.h"
#include "launcher.h"

bool window_is_manageable(HWND hwnd)
{
    if (hwnd == NULL || !IsWindow(hwnd)) {
        return false;
    }

    if (hwnd == launcher_get_hwnd()) {
        return false;
    }

    if (!IsWindowVisible(hwnd)) {
        return false;
    }

    LONG_PTR ex_style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if ((ex_style & WS_EX_TOOLWINDOW) != 0) {
        return false;
    }

    if (GetWindowTextLengthW(hwnd) == 0) {
        return false;
    }

    return true;
}

static BOOL CALLBACK enum_windows_proc(HWND hwnd, LPARAM lParam)
{
    (void)lParam;

    if (window_is_manageable(hwnd)) {
    }

    return TRUE;
}

void window_scan_manageable(void)
{
    EnumWindows(enum_windows_proc, 0);
}
