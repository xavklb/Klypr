#include "app.h"
#include "config.h"
#include "input.h"
#include "event.h"
#include "window.h"
#include "launcher.h"
#include <windows.h>

static bool s_running = false;

bool app_init(void)
{
    if (!config_init()) {
        return false;
    }

    if (!launcher_init(GetModuleHandleW(NULL))) {
        config_cleanup();
        return false;
    }

    if (!input_init()) {
        launcher_cleanup();
        config_cleanup();
        return false;
    }

    if (!event_init()) {
        input_cleanup();
        launcher_cleanup();
        config_cleanup();
        return false;
    }

    window_scan_manageable();
    s_running = true;

    return true;
}

int app_run(void)
{
    MSG msg;

    while (s_running && GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return 0;
}

void app_cleanup(void)
{
    s_running = false;
    event_cleanup();
    input_cleanup();
    launcher_cleanup();
    config_cleanup();
}

void app_stop(void)
{
    s_running = false;
    PostQuitMessage(0);
}
