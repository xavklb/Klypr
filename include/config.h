#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>

typedef struct {
    int gap_size;
    int border_width;
} AppConfig;

extern AppConfig g_config;

bool config_init(void);
void config_cleanup(void);

#endif
