#include <stdint.h>

bool JustifierOffscreen()
{
    return false;
}

uint32_t JustifierButtons(uint32_t *buttons)
{
    if (buttons)
        *buttons = 0;

    return 0;
}
