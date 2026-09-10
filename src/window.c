#include "window.h"
#include "layout.h"
#include "config.h"
#include "launcher.h"
#include <dwmapi.h>

#ifndef DWMWA_CLOAKED
#define DWMWA_CLOAKED 14
#endif

typedef struct {
    BspNode *root;
    HWND focused_window;
} Workspace;

static Workspace s_workspaces[WORKSPACE_COUNT];
static int s_active_workspace = 0;

static void bsp_hide_recursive(BspNode *node)
{
    if (node == NULL) {
        return;
    }
    if (node->window != NULL && IsWindow(node->window)) {
        ShowWindow(node->window, SW_HIDE);
    }
    bsp_hide_recursive(node->left);
    bsp_hide_recursive(node->right);
}

static void bsp_show_recursive(BspNode *node)
{
    if (node == NULL) {
        return;
    }
    if (node->window != NULL && IsWindow(node->window)) {
        ShowWindow(node->window, SW_SHOWNOACTIVATE);
    }
    bsp_show_recursive(node->left);
    bsp_show_recursive(node->right);
}

static HWND bsp_get_first_window(BspNode *node)
{
    if (node == NULL) {
        return NULL;
    }
    if (node->window != NULL && IsWindow(node->window)) {
        return node->window;
    }
    HWND left_win = bsp_get_first_window(node->left);
    if (left_win != NULL) {
        return left_win;
    }
    return bsp_get_first_window(node->right);
}

bool workspace_contains_window(int ws_index, HWND hwnd)
{
    if (ws_index < 0 || ws_index >= WORKSPACE_COUNT || hwnd == NULL) {
        return false;
    }
    return bsp_node_find(s_workspaces[ws_index].root, hwnd) != NULL;
}

static bool workspace_find_window(HWND hwnd, int *out_ws)
{
    if (hwnd == NULL) {
        return false;
    }
    for (int i = 0; i < WORKSPACE_COUNT; i++) {
        if (bsp_node_find(s_workspaces[i].root, hwnd) != NULL) {
            if (out_ws != NULL) {
                *out_ws = i;
            }
            return true;
        }
    }
    return false;
}

int workspace_get_active(void)
{
    return s_active_workspace;
}

void workspace_set_focused(HWND hwnd)
{
    if (workspace_contains_window(s_active_workspace, hwnd)) {
        s_workspaces[s_active_workspace].focused_window = hwnd;
    }
}

void window_arrange_active(void)
{
    RECT work_area;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work_area, 0);

    int gap = g_config.gap_size;
    int radius = g_config.border_radius;

    work_area.left += gap;
    work_area.top += gap;
    work_area.right -= gap;
    work_area.bottom -= gap;

    if (work_area.right <= work_area.left || work_area.bottom <= work_area.top) {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work_area, 0);
    }

    layout_arrange(s_workspaces[s_active_workspace].root, work_area, gap, radius);
}

void workspace_switch(int index)
{
    if (index < 0 || index >= WORKSPACE_COUNT || index == s_active_workspace) {
        return;
    }

    HWND current_fg = GetForegroundWindow();
    if (workspace_contains_window(s_active_workspace, current_fg)) {
        s_workspaces[s_active_workspace].focused_window = current_fg;
    }

    bsp_hide_recursive(s_workspaces[s_active_workspace].root);

    s_active_workspace = index;

    bsp_show_recursive(s_workspaces[s_active_workspace].root);
    window_arrange_active();

    HWND to_focus = s_workspaces[s_active_workspace].focused_window;
    if (to_focus == NULL || !IsWindow(to_focus)) {
        to_focus = bsp_get_first_window(s_workspaces[s_active_workspace].root);
    }
    if (to_focus != NULL && IsWindow(to_focus)) {
        SetForegroundWindow(to_focus);
        SetFocus(to_focus);
        s_workspaces[s_active_workspace].focused_window = to_focus;
    }
}

void window_add(HWND hwnd)
{
    if (!window_is_manageable(hwnd)) {
        return;
    }

    if (workspace_find_window(hwnd, NULL)) {
        return;
    }

    bsp_node_insert(&s_workspaces[s_active_workspace].root, hwnd, s_workspaces[s_active_workspace].focused_window);
    s_workspaces[s_active_workspace].focused_window = hwnd;
    window_arrange_active();
}

void window_remove(HWND hwnd)
{
    int ws = -1;
    if (!workspace_find_window(hwnd, &ws)) {
        return;
    }

    bsp_node_remove(&s_workspaces[ws].root, hwnd);

    if (s_workspaces[ws].focused_window == hwnd) {
        s_workspaces[ws].focused_window = bsp_get_first_window(s_workspaces[ws].root);
    }

    if (ws == s_active_workspace) {
        window_arrange_active();
        HWND to_focus = s_workspaces[ws].focused_window;
        if (to_focus != NULL && IsWindow(to_focus)) {
            SetForegroundWindow(to_focus);
            SetFocus(to_focus);
        }
    }
}

void window_focus_under_cursor(POINT pt)
{
    if (launcher_is_visible()) {
        return;
    }

    HWND hwnd = WindowFromPoint(pt);
    if (hwnd == NULL) {
        return;
    }

    hwnd = GetAncestor(hwnd, GA_ROOT);
    if (hwnd == NULL || !IsWindow(hwnd)) {
        return;
    }

    if (hwnd == launcher_get_hwnd()) {
        return;
    }

    HWND fg = GetForegroundWindow();
    if (hwnd == fg) {
        return;
    }

    if (!window_is_manageable(hwnd)) {
        return;
    }

    if (!workspace_contains_window(s_active_workspace, hwnd)) {
        return;
    }

    DWORD fg_thread = fg ? GetWindowThreadProcessId(fg, NULL) : 0;
    DWORD target_thread = GetWindowThreadProcessId(hwnd, NULL);
    DWORD cur_thread = GetCurrentThreadId();

    if (fg_thread != 0 && fg_thread != target_thread) {
        AttachThreadInput(cur_thread, fg_thread, TRUE);
    }
    if (target_thread != 0 && target_thread != cur_thread) {
        AttachThreadInput(cur_thread, target_thread, TRUE);
    }

    SetForegroundWindow(hwnd);
    SetFocus(hwnd);

    if (target_thread != 0 && target_thread != cur_thread) {
        AttachThreadInput(cur_thread, target_thread, FALSE);
    }
    if (fg_thread != 0 && fg_thread != target_thread) {
        AttachThreadInput(cur_thread, fg_thread, FALSE);
    }

    s_workspaces[s_active_workspace].focused_window = hwnd;
}

void window_close_active(void)
{
    HWND fg = GetForegroundWindow();
    if (fg == NULL || !IsWindow(fg) || fg == launcher_get_hwnd()) {
        return;
    }

    if (!workspace_contains_window(s_active_workspace, fg)) {
        return;
    }

    if (IsHungAppWindow(fg)) {
        DWORD pid = 0;
        GetWindowThreadProcessId(fg, &pid);
        if (pid != 0) {
            HANDLE proc = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
            if (proc != NULL) {
                TerminateProcess(proc, 1);
                CloseHandle(proc);
            }
        }
    } else {
        PostMessageW(fg, WM_CLOSE, 0, 0);
    }
}

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

    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    if ((style & WS_CHILD) != 0) {
        return false;
    }

    LONG_PTR ex_style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if ((ex_style & WS_EX_TOOLWINDOW) != 0) {
        return false;
    }

    if (GetWindowTextLengthW(hwnd) == 0) {
        return false;
    }

    if (hwnd == GetShellWindow() || hwnd == GetDesktopWindow()) {
        return false;
    }

    wchar_t class_name[256];
    if (GetClassNameW(hwnd, class_name, 256) > 0) {
        if (wcscmp(class_name, L"Shell_TrayWnd") == 0 ||
            wcscmp(class_name, L"Progman") == 0 ||
            wcscmp(class_name, L"WorkerW") == 0 ||
            wcscmp(class_name, L"KlyprLauncherClass") == 0) {
            return false;
        }
    }

    int cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) {
        return false;
    }

    return true;
}

static BOOL CALLBACK enum_windows_proc(HWND hwnd, LPARAM lParam)
{
    (void)lParam;

    if (window_is_manageable(hwnd)) {
        window_add(hwnd);
    }

    return TRUE;
}

void window_scan_manageable(void)
{
    EnumWindows(enum_windows_proc, 0);
}

bool window_init(void)
{
    for (int i = 0; i < WORKSPACE_COUNT; i++) {
        s_workspaces[i].root = NULL;
        s_workspaces[i].focused_window = NULL;
    }
    s_active_workspace = 0;
    SystemParametersInfoW(SPI_SETFOREGROUNDLOCKTIMEOUT, 0, 0, SPIF_SENDCHANGE);
    return true;
}

void window_cleanup(void)
{
    for (int i = 0; i < WORKSPACE_COUNT; i++) {
        if (s_workspaces[i].root != NULL) {
            bsp_node_destroy(s_workspaces[i].root);
            s_workspaces[i].root = NULL;
        }
        s_workspaces[i].focused_window = NULL;
    }
}
