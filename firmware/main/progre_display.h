#pragma once

#include <stdbool.h>
#include "esp_err.h"

esp_err_t progre_display_init(void);

void progre_display_show_companion(
    int x,
    bool active,
    bool blink,
    bool step
);

void progre_display_show_banner(
    const char *text,
    int scroll_x
);

#include <stdint.h>
void progre_display_set_thinking(bool thinking);
void progre_display_set_level(unsigned level);
void progre_display_set_color(uint16_t color);
