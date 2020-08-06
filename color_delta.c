#include "color_delta.h"
#include "pngloss_image.h"

void color_difference(
    uint_fast8_t bytes_per_pixel, color_delta difference,
    int_fast16_t *back_color, int_fast16_t *here_color
) {
    int_fast16_t d;
    switch (bytes_per_pixel) {
        case 1:
            // grayscale
            d = here_color[0] - back_color[0];
            difference[0] = d;
            difference[1] = d;
            difference[2] = d;
            difference[3] = 0;
            break;
        case 2:
            // gray + alpha
            d = here_color[0] - back_color[0];
            difference[0] = d;
            difference[1] = d;
            difference[2] = d;
            difference[3] = here_color[1] - back_color[1];
            break;
        case 3:
            // rgb
            difference[0] = here_color[0] - back_color[0];
            difference[1] = here_color[1] - back_color[1];
            difference[2] = here_color[2] - back_color[2];
            difference[3] = 0;
            break;
        case 4:
            // rgba
            difference[0] = here_color[0] - back_color[0];
            difference[1] = here_color[1] - back_color[1];
            difference[2] = here_color[2] - back_color[2];
            difference[3] = here_color[3] - back_color[3];
            break;
    }
}

void color_delta_difference(
    color_delta back_delta, color_delta here_delta, color_d2 d2
) {
    d2[0] = here_delta[0] - back_delta[0];
    d2[1] = here_delta[1] - back_delta[1];
    d2[2] = here_delta[2] - back_delta[2];
    d2[3] = here_delta[3] - back_delta[3];
}

uint32_t color_distance(color_delta difference) {
    uint32_t total = 0;
    for (uint_fast8_t i = 0; i < 4; i++) {
        total += difference[i] * difference[i];
    }
    return total;
}

uint32_t color_delta_distance(color_d2 partial) {
    uint32_t total = 0;
    for (uint_fast8_t i = 0; i < 4; i++) {
        total += partial[i] * partial[i];
    }
    return total;
}

