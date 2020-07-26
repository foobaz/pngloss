#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "optimize_state.h"

const uint_fast8_t dither_row_count = 3;
const uint_fast8_t dither_filter_width = 5;
const uint_fast16_t symbol_count = 285;

pngloss_error optimize_state_init(
    optimize_state *state, pngloss_image *image
) {
    state->x = 0;
    state->y = 0;
    state->symbol_count = 0;

    // clear values in case we return early and later free uninitialized pointers
    state->pixels = NULL;
    state->color_error = NULL;
    state->symbol_frequency = NULL;

    state->pixels = calloc((size_t)image->width, bytes_per_pixel);
    if (!state->pixels) {
        return OUT_OF_MEMORY_ERROR;
    }

    uint32_t error_width = image->width + dither_filter_width - 1;
    state->color_error = calloc((size_t)dither_row_count * error_width, sizeof(color_delta));
    if (!state->color_error) {
        return OUT_OF_MEMORY_ERROR;
    }

    state->symbol_frequency = calloc(symbol_count, sizeof(uint32_t));
    if (!state->symbol_frequency) {
        return OUT_OF_MEMORY_ERROR;
    }

    return SUCCESS;
}

void optimize_state_destroy(optimize_state *state) {
    free(state->pixels);
    free(state->color_error);
    free(state->symbol_frequency);
}

void optimize_state_copy(
    optimize_state *to,
    optimize_state *from,
    pngloss_image *image
) {
    to->x = from->x;
    to->y = from->y;

    memcpy(to->pixels, from->pixels, (size_t)image->width * bytes_per_pixel);

    uint32_t error_width = image->width + dither_filter_width - 1;
    memcpy(to->color_error, from->color_error, (size_t)dither_row_count * error_width * sizeof(color_delta));

    memcpy(to->symbol_frequency, from->symbol_frequency, (size_t)symbol_count * sizeof(uint32_t));
    to->symbol_count = from->symbol_count;
}

uint_fast8_t optimize_state_run(
    optimize_state *state, pngloss_image *image,
    uint_fast8_t quantization_strength, pngloss_filter filter
) {
    int_fast16_t back_color[4];
    int_fast16_t here_color[4];
    uint_fast8_t symbol_cost = 0;
    for (uint_fast8_t c = 0; c < bytes_per_pixel; c++) {
        uint32_t offset = state->x*bytes_per_pixel + c;
        back_color[c] = image->rows[state->y][offset];
        here_color[c] = back_color[c] + state->color_error[state->x+dither_filter_width/2][c];

        unsigned char left = 0;
        if (state->x > 0) {
            left = state->pixels[offset-bytes_per_pixel];
        }
        unsigned char predicted = filter_predict(image, state->x, state->y, filter, c, left);
        int_fast16_t filtered_value = here_color[c] - predicted;

        int_fast16_t original = back_color[c] - predicted;
        unsigned char best_close_value = original;

        int_fast16_t strength = quantization_strength;
        if (c == 1) {
            // human eye is most sensitive to green
            strength /= 2;
        }
        int_fast16_t min = (filtered_value * 2 - (int_fast16_t)strength) / 2;
        // include original because it's known to be in 0-255
        if (min > original) {
            min = original;
        }
        int_fast16_t max = (filtered_value * 2 + (int_fast16_t)strength + 1) / 2;
        if (max < original) {
            max = original;
        }
        //fprintf(stderr, "%u starting %d min %d max %d\n", (unsigned int)c, (int)original, (int)min, (int)max); 
        int_fast16_t window_width = 1 + max - min;
        uint32_t best_frequency = 0;
        for (int_fast16_t close_value = min; close_value <= max; close_value++) {
            int_fast16_t back = close_value + predicted;
            if (back >= 0 && back <= 255) {
                unsigned long frequency = state->symbol_frequency[(unsigned char)close_value];
                unsigned long bias = abs(close_value - filtered_value);
                if (best_frequency + bias < frequency) {
                //if (best_frequency < frequency) {
                    best_frequency = frequency;
                    best_close_value = close_value;
                    back_color[c] = back;
                }
            }
        }
        //fprintf(stderr, "%u orig %u here %d best %d\n", (unsigned int)c, (unsigned int)image->rows[state->y][offset], (int)here_color[c], (int)back_color[c]); 

        state->pixels[offset] = back_color[c];

        state->symbol_frequency[best_close_value]++;
        //fprintf(stderr, "%u best %u has frequency %u\n", (unsigned int)c, (unsigned int)best_close_value, (unsigned int)state->symbol_frequency[best_close_value]);
        state->symbol_count++;
        //fprintf(stderr, "val %u, count %u, freq %u\n", (unsigned int)back_value, (unsigned int)state->symbol_count, (unsigned int)state->symbol_frequency[back_value]);
        symbol_cost += ulog2(state->symbol_count / state->symbol_frequency[best_close_value]);
    }

    state->x++;

    color_delta difference;
    color_difference(back_color, here_color, difference);
    diffuse_color_error(state, image, difference);

    return symbol_cost;
}

uint32_t optimize_state_row(
    optimize_state *state, pngloss_image *image,
    uint_fast8_t quantization_strength, uint32_t best_cost,
    pngloss_filter filter
) {
    uint32_t total_cost = 0;
    while (state->x < image->width) {
        uint_fast8_t cost = optimize_state_run(
            state,
            image,
            quantization_strength,
            filter
        );
        total_cost += cost;
        //fprintf(stderr, "In %s, cost == %d, total_cost == %d\n", __func__, (int)cost, (int)total_cost);
        if (best_cost <= total_cost) {
            //fprintf(stderr, "returning early, best_cost == %u, total_cost == %u\n", (unsigned int)best_cost, (unsigned int)total_cost);
            //return -1;
        }
    }

    unsigned char *above_row = NULL;
    if (state->y > 0) {
        above_row = image->rows[state->y - 1];
    }
    uint_fast8_t adaptive_filter = adaptive_filter_for_rows(image, above_row, state->pixels);
    //fprintf(stderr, "finished row %u, cost == %u, filter == 0x%X\n", (unsigned int)state->y, (unsigned int)total_cost, (unsigned int)adaptive_filter);

    if (filter != adaptive_filter) {
        return -1;
    }

    // advance to next row and indicate success and cost to caller
    state->x = 0;
    state->y++;

    // move color errors up one row
    uint32_t error_width = image->width + dither_filter_width - 1;
    memmove(
        state->color_error,
        state->color_error + error_width,
        (dither_row_count - 1) * error_width * sizeof(color_delta)
    );
    memset(state->color_error + (dither_row_count - 1) * error_width, 0, error_width * sizeof(color_delta));
    return total_cost;
}

unsigned char filter_predict(
    pngloss_image *image, uint32_t x, uint32_t y,
    pngloss_filter filter, uint_fast8_t c, unsigned char left
) {
    uint32_t offset = x*bytes_per_pixel + c;
    unsigned char above = 0, diag = 0;
    if (y > 0) {
        above = image->rows[y-1][offset];
        if (x > 0) {
            diag = image->rows[y-1][offset-bytes_per_pixel];
        }
    }

    unsigned char (*filter_functions[pngloss_filter_count])(
        unsigned char, unsigned char, unsigned char
    ) = {
        pngloss_filter_none,
        pngloss_filter_sub,
        pngloss_filter_up,
        pngloss_filter_average,
        pngloss_filter_paeth,
    };

    unsigned char predicted = filter_functions[filter](above, diag, left);
    return predicted;
}

void diffuse_color_error(
    optimize_state *state, pngloss_image *image, color_delta difference
) {
    uint32_t error_width = image->width + dither_filter_width - 1;

    for (uint_fast8_t c = 0; c < bytes_per_pixel; c++) {
        int_fast16_t d = difference[c];

        /*
        // floyd-steinberg dithering
        int_fast16_t one = d / 16;
        d -= one;
        state->color_error[error_width + state->x + 3][c] += one;

        int_fast16_t three = d / 5;
        d -= three;
        state->color_error[error_width + state->x + 1][c] += three;

        int_fast16_t five = d * 5/12;
        d -= five;
        state->color_error[error_width + state->x + 2][c] += five;

        int_fast16_t seven = d;
        state->color_error[state->x + 3][c] += seven;
        */

        /*
        // two-row sierra dithering
        int_fast16_t ones = d / 16;
        d -= ones * 2;
        state->color_error[error_width + state->x + 0][c] += ones;
        state->color_error[error_width + state->x + 4][c] += ones;

        //int_fast16_t twos = d / 8;
        int_fast16_t twos = d / 7;
        d -= twos * 2;
        state->color_error[error_width + state->x + 1][c] += twos;
        state->color_error[error_width + state->x + 3][c] += twos;

        //int_fast16_t threes = d * 3/16;
        int_fast16_t threes = d * 3/10;
        d -= threes * 2;
        state->color_error[error_width + state->x + 2][c] += threes;
        state->color_error[state->x + 4][c] += threes;

        //int_fast16_t four = d / 4;
        int_fast16_t four = d;
        state->color_error[state->x + 3][c] += four;
        */

        /*
        // sierra dithering
        int_fast16_t twos = d / 16;
        d -= twos * 4;
        state->color_error[error_width * 1 + state->x + 0][c] += twos;
        state->color_error[error_width * 1 + state->x + 4][c] += twos;
        state->color_error[error_width * 2 + state->x + 1][c] += twos;
        state->color_error[error_width * 2 + state->x + 3][c] += twos;

        int_fast16_t threes = d / 8;
        d -= threes * 2;
        state->color_error[error_width * 0 + state->x + 4][c] += threes;
        state->color_error[error_width * 3 + state->x + 2][c] += threes;

        int_fast16_t fours = d * 2/9;
        d -= fours * 2;
        state->color_error[error_width * 1 + state->x + 1][c] += fours;
        state->color_error[error_width * 1 + state->x + 3][c] += fours;

        int_fast16_t five = d / 2;
        d -= five;
        state->color_error[error_width * 1 + state->x + 2][c] += five;

        state->color_error[error_width * 0 + state->x + 3][c] += d;
        */

        // sierra dithering, reduced color bleed
        int_fast16_t twos = d / 16;
        state->color_error[error_width * 1 + state->x + 0][c] += twos;
        state->color_error[error_width * 1 + state->x + 4][c] += twos;
        state->color_error[error_width * 2 + state->x + 1][c] += twos;
        state->color_error[error_width * 2 + state->x + 3][c] += twos;

        int_fast16_t threes = d * 3 / 32;
        state->color_error[error_width * 0 + state->x + 4][c] += threes;
        state->color_error[error_width * 3 + state->x + 2][c] += threes;

        int_fast16_t fours = d / 8;
        state->color_error[error_width * 1 + state->x + 1][c] += fours;
        state->color_error[error_width * 1 + state->x + 3][c] += fours;

        int_fast16_t five = d * 5 / 32;
        state->color_error[error_width * 0 + state->x + 3][c] += five;
        state->color_error[error_width * 1 + state->x + 2][c] += five;
    }
}

uint_fast8_t adaptive_filter_for_rows(
    pngloss_image *image, unsigned char *above_row, unsigned char *pixels
) {
    uint32_t none_sum = 0, sub_sum = 0, up_sum = 0;
    uint32_t average_sum = 0, paeth_sum = 0;

    for (uint32_t i = 0; i < image->width * bytes_per_pixel; i++) {
        unsigned char above = 0, left = 0, diag = 0;
        if (i >= bytes_per_pixel) {
            left = pixels[i-bytes_per_pixel];
            if (above_row) {
                diag = above_row[i-bytes_per_pixel];
            }
        }
        if (above_row) {
            above = above_row[i];
        }
        unsigned char here = pixels[i];
        none_sum += (here < 128) ? here : (256 - here);

        unsigned char sub = here - left;
        sub_sum += (sub < 128) ? sub : (256 - sub);

        unsigned char up = here - above;
        up_sum += (up < 128) ? up : (256 - up);

        unsigned char average = here - (left + above) / 2;
        average_sum += (average < 128) ? average : (256 - average);

        int_fast16_t p = above - diag;
        int_fast16_t p_diag = left - diag;
        int_fast16_t p_left = p < 0 ? -p : p;
        int_fast16_t p_above = p_diag < 0 ? -p_diag : p_diag;
        p_diag = (p + p_diag) < 0 ? -(p + p_diag) : p + p_diag;
        int_fast16_t predicted = (p_left <= p_above && p_left <= p_diag) ? left : (p_above <= p_diag) ? above : diag;
        unsigned char paeth = here - predicted;

        paeth_sum += (paeth < 128) ? paeth : 256 - paeth;
    }
    uint32_t min_sum = none_sum;
    if (min_sum > sub_sum) {
        min_sum = sub_sum;
    }
    if (min_sum > up_sum) {
        min_sum = up_sum;
    }
    if (min_sum > average_sum) {
        min_sum = average_sum;
    }
    if (min_sum > paeth_sum) {
        min_sum = paeth_sum;
    }

    if (min_sum >= none_sum) {
        return pngloss_none;
    }
    if (min_sum >= sub_sum) {
        return pngloss_sub;
    }
    if (min_sum >= up_sum) {
        return pngloss_up;
    }
    if (min_sum >= average_sum) {
        return pngloss_average;
    }
    if (min_sum >= paeth_sum) {
        return pngloss_paeth;
    }
    // unreachable
    return pngloss_filter_count;
}

// calculates floor(log2(x))
uint_fast8_t ulog2(unsigned long x) {
    uint_fast8_t result = 0;
    while (x) {
        x >>= 1;
        result += 1;
    }
    return result;
}

// PNG filters
unsigned char pngloss_filter_none(
    unsigned char above, unsigned char diag, unsigned char left
) {
#pragma unused(above, diag, left)
    return 0;
}

unsigned char pngloss_filter_sub(
    unsigned char above, unsigned char diag, unsigned char left
) {
#pragma unused(above, diag)
    return left;
}

unsigned char pngloss_filter_up(
    unsigned char above, unsigned char diag, unsigned char left
) {
#pragma unused(diag, left)
    return above;
}

unsigned char pngloss_filter_average(
    unsigned char above, unsigned char diag, unsigned char left
) {
#pragma unused(diag)
    return (above + left) / 2;
}

unsigned char pngloss_filter_paeth(
    unsigned char above, unsigned char diag, unsigned char left
) {
    int_fast16_t p = above - diag;
    int_fast16_t p_diag = left - diag;
    int_fast16_t p_left = p < 0 ? -p : p;
    int_fast16_t p_above = p_diag < 0 ? -p_diag : p_diag;
    p_diag = (p + p_diag) < 0 ? -(p + p_diag) : p + p_diag;
    int_fast16_t predicted = (p_left <= p_above && p_left <= p_diag) ? left : (p_above <= p_diag) ? above : diag;
    return predicted;
}

