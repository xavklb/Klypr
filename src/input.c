#include "input.h"
#include "app.h"
#include "config.h"
#include "launcher.h"
#include <windows.h>
#include <shellapi.h>

#ifndef MOD_NOREPEAT
#define MOD_NOREPEAT 0x4000
#endif

static HHOOK s_keyboard_hook = NULL;
static bool s_hotkey_terminal_registered = false;
static bool s_hotkey_quit_registered = false;

static bool try_launch(const wchar_t *input)
{
    if (input == NULL || input[0] == L'\0') {
        return false;
    }

    wchar_t cmd[MAX_PATH];
    const wchar_t *params = NULL;

    if (input[0] == L'"') {
        const wchar_t *end_quote = wcschr(input + 1, L'"');
        if (end_quote != NULL) {
            size_t len = (size_t)(end_quote - (input + 1));
            if (len >= MAX_PATH) {
                len = MAX_PATH - 1;
            }
            wcsncpy(cmd, input + 1, len);
            cmd[len] = L'\0';
            params = end_quote + 1;
            while (*params == L' ' || *params == L'\t') {
                params++;
            }
            if (*params == L'\0') {
                params = NULL;
            }
        } else {
            wcsncpy(cmd, input + 1, MAX_PATH - 1);
            cmd[MAX_PATH - 1] = L'\0';
        }
    } else {
        const wchar_t *space = wcschr(input, L' ');
        if (space != NULL) {
            size_t len = (size_t)(space - input);
            if (len >= MAX_PATH) {
                len = MAX_PATH - 1;
            }
            wcsncpy(cmd, input, len);
            cmd[len] = L'\0';
            params = space + 1;
            while (*params == L' ' || *params == L'\t') {
                params++;
            }
            if (*params == L'\0') {
                params = NULL;
            }
        } else {
            wcsncpy(cmd, input, MAX_PATH - 1);
            cmd[MAX_PATH - 1] = L'\0';
        }
    }

    HINSTANCE h = ShellExecuteW(NULL, L"open", cmd, params, NULL, SW_SHOWNORMAL);
    return ((INT_PTR)h > 32);
}

void input_launch_terminal(void)
{
    // 1. Configured terminal from klypr.ini
    if (g_config.terminal[0] != L'\0' && try_launch(g_config.terminal)) {
        return;
    }

    // 2. Windows Terminal (wt.exe)
    if (try_launch(L"wt.exe")) {
        return;
    }

    // 3. Windows Terminal via AppX package alias if wt.exe was not in PATH or restricted
    if (try_launch(L"explorer.exe shell:AppsFolder\\Microsoft.WindowsTerminal_8wekyb3d8bbwe!App")) {
        return;
    }

    // 4. PowerShell
    if (try_launch(L"powershell.exe")) {
        return;
    }

    // 5. Command Prompt fallback
    try_launch(L"cmd.exe");
}

void input_handle_hotkey(int hotkey_id)
{
    switch (hotkey_id) {
    case HOTKEY_ID_TERMINAL:
        input_launch_terminal();
        break;
    case HOTKEY_ID_QUIT:
        app_stop();
        break;
    default:
        break;
    }
}

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

        // Low-level hook fallback if RegisterHotKey was not caught
        if (alt_down && !shift_down && kb->vkCode == VK_RETURN) {
            input_launch_terminal();
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
    // Register global hotkeys (MOD_NOREPEAT prevents repeat firing when key is held)
    // RegisterHotKey is kernel-level: it never drops from timeouts or AV heuristics
    s_hotkey_terminal_registered = RegisterHotKey(NULL, HOTKEY_ID_TERMINAL, MOD_ALT | MOD_NOREPEAT, VK_RETURN) != 0;
    s_hotkey_quit_registered = RegisterHotKey(NULL, HOTKEY_ID_QUIT, MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, 'Q') != 0;

    // Low-level hook handles Ctrl+A (where we need to check the active window) and serves as hook backup
    s_keyboard_hook = SetWindowsHookExW(
        WH_KEYBOARD_LL,
        keyboard_proc,
        GetModuleHandleW(NULL),
        0
    );

    return (s_keyboard_hook != NULL) || s_hotkey_terminal_registered;
}

void input_cleanup(void)
{
    if (s_hotkey_terminal_registered) {
        UnregisterHotKey(NULL, HOTKEY_ID_TERMINAL);
        s_hotkey_terminal_registered = false;
    }

    if (s_hotkey_quit_registered) {
        UnregisterHotKey(NULL, HOTKEY_ID_QUIT);
        s_hotkey_quit_registered = false;
    }

    if (s_keyboard_hook != NULL) {
        UnhookWindowsHookEx(s_keyboard_hook);
        s_keyboard_hook = NULL;
    }
}
