#ifndef INPUT_H
#define INPUT_H

#include <stdbool.h>

#define HOTKEY_ID_TERMINAL 1001
#define HOTKEY_ID_QUIT     1002

bool input_init(void);
void input_cleanup(void);
void input_handle_hotkey(int hotkey_id);
void input_launch_terminal(void);

#endif
