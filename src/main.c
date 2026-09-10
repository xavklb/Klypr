#include "app.h"
#include <windows.h>

int WINAPI WinMain(
    HINSTANCE hInstance,
    HINSTANCE hPrevInstance,
    LPSTR lpCmdLine,
    int nShowCmd
)
{
    (void)hInstance;
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nShowCmd;

    if (!app_init()) {
        return 0;
    }

    int exit_code = app_run();

    app_cleanup();

    return exit_code;
}
