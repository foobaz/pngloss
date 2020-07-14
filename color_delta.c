#include "color_delta.h"

/*
void diffuse_color_deltas(color_delta **color_error, uint32_t x, int32_t *delta) {
    // Sierra dithering
    for(uint_fast8_t i = 0; i < 4; i++) {
        delta[i] += 2 * color_error[2][x-1][i];
        delta[i] += 3 * color_error[2][x][i];
        delta[i] += 2 * color_error[2][x+1][i];
        delta[i] += 2 * color_error[1][x-2][i];
        delta[i] += 4 * color_error[1][x-1][i];
        delta[i] += 5 * color_error[1][x][i];
        delta[i] += 4 * color_error[1][x+1][i];
        delta[i] += 2 * color_error[1][x+2][i];
        delta[i] += 3 * color_error[0][x-2][i];
        delta[i] += 5 * color_error[0][x-1][i];
        if (delta[i] < 0) {
            delta[i] -= 16;
        } else {
            delta[i] += 16;
        }
        delta[i] /= 32;
    }
}
*/

void color_difference(
    unsigned char *back_color, int_fast16_t *here_color,
    color_delta difference, uint_fast8_t bytes_per_pixel
) {
    int32_t d;
    switch (bytes_per_pixel) {
        case 1:
            // grayscale
            d = (int32_t)here_color[0] - (int32_t)back_color[0];
            difference[0] = d;
            difference[1] = d;
            difference[2] = d;
            difference[3] = 0;
            break;
        case 2:
            // gray + alpha
            d = (int32_t)here_color[0] - (int32_t)back_color[0];
            difference[0] = d;
            difference[1] = d;
            difference[2] = d;
            difference[3] = (int32_t)here_color[3] - (int32_t)back_color[3];
            break;
        case 3:
            // rgb
            difference[0] = (int32_t)here_color[0] - (int32_t)back_color[0];
            difference[1] = (int32_t)here_color[1] - (int32_t)back_color[1];
            difference[2] = (int32_t)here_color[2] - (int32_t)back_color[2];
            difference[3] = 0;
            break;
        case 4:
            // rgba
            difference[0] = (int32_t)here_color[0] - (int32_t)back_color[0];
            difference[1] = (int32_t)here_color[1] - (int32_t)back_color[1];
            difference[2] = (int32_t)here_color[2] - (int32_t)back_color[2];
            difference[3] = (int32_t)here_color[3] - (int32_t)back_color[3];
            break;
    }
}

uint32_t color_distance(color_delta difference) {
    uint32_t total = 0;
    for (uint_fast8_t i = 0; i < 4; i++) {
        total += difference[i] * difference[i];
    }
    return total;
}

