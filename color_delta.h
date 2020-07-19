#ifndef COLOR_DELTA_H
#define COLOR_DELTA_H

#include <stdint.h>

typedef int_least16_t color_delta[4];
typedef int_least16_t color_d2[4];

void color_difference(
    int_fast16_t *back_color, int_fast16_t *here_color,
    color_delta difference
);
void color_delta_difference(
    color_delta back_delta, color_delta here_delta,
    color_d2 d2
);
uint32_t color_distance(color_delta difference);
uint32_t color_delta_distance(color_delta difference);

#endif // COLOR_DELTA_H
