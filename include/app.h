#ifndef APP_H
#define APP_H

#include <stdbool.h>

bool app_init(void);
int app_run(void);
void app_cleanup(void);
void app_stop(void);

#endif
