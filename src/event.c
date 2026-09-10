#include "event.h"
#include "window.h"
#include <windows.h>

static HWINEVENTHOOK s_winevent_hook = NULL;

static void CALLBACK winevent_proc(
    HWINEVENTHOOK hWinEventHook,
    DWORD event,
    HWND hwnd,
    LONG idObject,
    LONG idChild,
    DWORD idEventThread,
    DWORD dwmsEventTime
)
{
    (void)hWinEventHook;
    (void)idEventThread;
    (void)dwmsEventTime;

    if (idObject != OBJID_WINDOW || idChild != CHILDID_SELF || hwnd == NULL) {
        return;
    }

    if (event == EVENT_OBJECT_SHOW) {
        if (window_is_manageable(hwnd)) {
            window_add(hwnd);
        }
    } else if (event == EVENT_OBJECT_DESTROY) {
        window_remove(hwnd);
    }
}

bool event_init(void)
{
    DWORD min_event = EVENT_OBJECT_DESTROY < EVENT_OBJECT_SHOW ? EVENT_OBJECT_DESTROY : EVENT_OBJECT_SHOW;
    DWORD max_event = EVENT_OBJECT_DESTROY > EVENT_OBJECT_SHOW ? EVENT_OBJECT_DESTROY : EVENT_OBJECT_SHOW;

    s_winevent_hook = SetWinEventHook(
        min_event,
        max_event,
        NULL,
        winevent_proc,
        0,
        0,
        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS
    );

    return s_winevent_hook != NULL;
}

void event_cleanup(void)
{
    if (s_winevent_hook != NULL) {
        UnhookWinEvent(s_winevent_hook);
        s_winevent_hook = NULL;
    }
}
