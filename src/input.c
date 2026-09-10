#include "input.h"
#include "app.h"
#include "config.h"
#include "launcher.h"
#include "window.h"
#include <windows.h>

static HHOOK s_keyboard_hook = NULL;
static HHOOK s_mouse_hook = NULL;

static LRESULT CALLBACK keyboard_proc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
        KBDLLHOOKSTRUCT *kb = (KBDLLHOOKSTRUCT *)lParam;
        bool ctrl_down = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        bool alt_down = ((GetAsyncKeyState(VK_MENU) & 0x8000) != 0) || ((kb->flags & LLKHF_ALTDOWN) != 0);
        bool shift_down = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

        if (ctrl_down && !alt_down && !shift_down && (kb->vkCode == 'A' || kb->vkCode == 'a')) {
            if (GetForegroundWindow() == launcher_get_hwnd()) {
                return CallNextHookEx(s_keyboard_hook, nCode, wParam, lParam);
            }
            launcher_show();
            return 1;
        }

        if (alt_down && shift_down && !ctrl_down && (kb->vkCode == 'Q' || kb->vkCode == 'q')) {
            app_stop();
            return 1;
        }

        if (alt_down && !shift_down && !ctrl_down && (kb->vkCode == 'Q' || kb->vkCode == 'q')) {
            window_close_active();
            return 1;
        }

        if (alt_down && !shift_down && !ctrl_down && kb->vkCode == VK_RETURN) {
            launcher_execute(g_config.terminal);
            return 1;
        }

        if (alt_down && !shift_down && !ctrl_down) {
            if (kb->vkCode >= '1' && kb->vkCode <= '9') {
                workspace_switch(kb->vkCode - '1');
                return 1;
            }
            if (kb->vkCode >= VK_NUMPAD1 && kb->vkCode <= VK_NUMPAD9) {
                workspace_switch(kb->vkCode - VK_NUMPAD1);
                return 1;
            }
        }
    }

    return CallNextHookEx(s_keyboard_hook, nCode, wParam, lParam);
}

static LRESULT CALLBACK mouse_proc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION && wParam == WM_MOUSEMOVE) {
        MSLLHOOKSTRUCT *ms = (MSLLHOOKSTRUCT *)lParam;
        window_focus_under_cursor(ms->pt);
    }

    return CallNextHookEx(s_mouse_hook, nCode, wParam, lParam);
}

bool input_init(void)
{
    HINSTANCE hInst = GetModuleHandleW(NULL);

    s_keyboard_hook = SetWindowsHookExW(
        WH_KEYBOARD_LL,
        keyboard_proc,
        hInst,
        0
    );

    s_mouse_hook = SetWindowsHookExW(
        WH_MOUSE_LL,
        mouse_proc,
        hInst,
        0
    );

    return (s_keyboard_hook != NULL) && (s_mouse_hook != NULL);
}

void input_cleanup(void)
{
    if (s_keyboard_hook != NULL) {
        UnhookWindowsHookEx(s_keyboard_hook);
        s_keyboard_hook = NULL;
    }

    if (s_mouse_hook != NULL) {
        UnhookWindowsHookEx(s_mouse_hook);
        s_mouse_hook = NULL;
    }
}
