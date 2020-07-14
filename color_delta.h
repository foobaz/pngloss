#ifndef COLOR_DELTA_H
#define COLOR_DELTA_H

#include <stdint.h>

typedef int_least16_t color_delta[4];

void diffuse_color_deltas(
    color_delta **color_error, uint32_t x, int32_t *delta
);
void color_difference(
    unsigned char *back_color, int_fast16_t *here_color,
    color_delta difference, uint_fast8_t bytes_per_pixel
);
uint32_t color_distance(color_delta difference);

#endif // COLOR_DELTA_H
