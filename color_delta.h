#ifndef COLOR_DELTA_H
#define COLOR_DELTA_H

#include <stdint.h>

typedef int_least16_t color_delta[4];

void color_difference(
    int_fast16_t *back_color, int_fast16_t *here_color,
    color_delta difference, uint_fast8_t bytes_per_pixel
);
uint32_t color_distance(color_delta difference);

#endif // COLOR_DELTA_H
