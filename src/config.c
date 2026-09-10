#include "config.h"

AppConfig g_config = {
    .gap_size = 8,
    .border_width = 2
};

bool config_init(void)
{
    return true;
}

void config_cleanup(void)
{
}
