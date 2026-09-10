#include "app.h"
#include "config.h"
#include "input.h"
#include "launcher.h"
#include "tray.h"
#include <stdio.h>
#include <windows.h>

#define KLYPR_MUTEX_NAME L"Local\\Klypr_SingleInstance_Mutex"

static bool s_running = false;
static HANDLE s_instance_mutex = NULL;

bool app_init(void)
{
    // Enforce single-instance
    s_instance_mutex = CreateMutexW(NULL, TRUE, KLYPR_MUTEX_NAME);
    if (s_instance_mutex == NULL || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (s_instance_mutex != NULL) {
            CloseHandle(s_instance_mutex);
            s_instance_mutex = NULL;
        }

        // If an instance is already running, bring up its launcher
        HWND hwnd_existing = FindWindowW(L"KlyprLauncherClass", NULL);
        if (hwnd_existing != NULL) {
            PostMessageW(hwnd_existing, WM_KLYPR_SHOW, 0, 0);
        }

        return false;
    }

    // Initialize COM for ShellExecute and modern packaged apps
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    if (!config_init()) {
        if (s_instance_mutex != NULL) {
            ReleaseMutex(s_instance_mutex);
            CloseHandle(s_instance_mutex);
            s_instance_mutex = NULL;
        }
        CoUninitialize();
        return false;
    }

    if (!launcher_init(GetModuleHandleW(NULL))) {
        config_cleanup();
        if (s_instance_mutex != NULL) {
            ReleaseMutex(s_instance_mutex);
            CloseHandle(s_instance_mutex);
            s_instance_mutex = NULL;
        }
        CoUninitialize();
        return false;
    }

    if (!input_init()) {
        launcher_cleanup();
        config_cleanup();
        if (s_instance_mutex != NULL) {
            ReleaseMutex(s_instance_mutex);
            CloseHandle(s_instance_mutex);
            s_instance_mutex = NULL;
        }
        CoUninitialize();
        return false;
    }

    tray_init(launcher_get_hwnd());

    s_running = true;

    return true;
}

int app_run(void)
{
    MSG msg;

    while (s_running && GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (msg.message == WM_HOTKEY) {
            input_handle_hotkey((int)msg.wParam);
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return 0;
}

void app_cleanup(void)
{
    s_running = false;
    tray_cleanup();
    input_cleanup();
    launcher_cleanup();
    config_cleanup();

    if (s_instance_mutex != NULL) {
        ReleaseMutex(s_instance_mutex);
        CloseHandle(s_instance_mutex);
        s_instance_mutex = NULL;
    }

    CoUninitialize();
}

void app_stop(void)
{
    s_running = false;
    PostQuitMessage(0);
}
