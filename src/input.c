#include "input.h"
#include "app.h"
#include <windows.h>
#include <shellapi.h>

static HHOOK s_keyboard_hook = NULL;

static LRESULT CALLBACK keyboard_proc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
        KBDLLHOOKSTRUCT *kb = (KBDLLHOOKSTRUCT *)lParam;
        bool alt_down = ((GetAsyncKeyState(VK_MENU) & 0x8000) != 0) || ((kb->flags & LLKHF_ALTDOWN) != 0);
        bool shift_down = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

        if (alt_down && !shift_down && kb->vkCode == VK_RETURN) {
            ShellExecuteW(NULL, L"open", L"wt.exe", NULL, NULL, SW_SHOWNORMAL);
            return 1;
        }

        if (alt_down && shift_down && (kb->vkCode == 'Q' || kb->vkCode == 'q')) {
            app_stop();
            return 1;
        }
    }

    return CallNextHookEx(s_keyboard_hook, nCode, wParam, lParam);
}

bool input_init(void)
{
    s_keyboard_hook = SetWindowsHookExW(
        WH_KEYBOARD_LL,
        keyboard_proc,
        GetModuleHandleW(NULL),
        0
    );

    return s_keyboard_hook != NULL;
}

void input_cleanup(void)
{
    if (s_keyboard_hook != NULL) {
        UnhookWindowsHookEx(s_keyboard_hook);
        s_keyboard_hook = NULL;
    }
}
