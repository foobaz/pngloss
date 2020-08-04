/**
 © 2020 William MacKay.
 Based on algorithms by Michael Vinther and William MacKay.

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 See the GNU General Public License for more details:
 <http://www.gnu.org/copyleft/gpl.html>
*/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "optimize_state.h"

const uint_fast8_t dither_row_count = 3;
const uint_fast8_t dither_filter_width = 5;
const uint_fast16_t symbol_count = 256;

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
    for (uint_fast8_t filter = 0; filter < 5; filter++) {
        state->original_frequency[filter] = NULL;
    }

    state->pixels = calloc((size_t)image->width, image->bytes_per_pixel);
    if (!state->pixels) {
        return OUT_OF_MEMORY_ERROR;
    }

    uint32_t error_width = image->width + dither_filter_width;
    state->color_error = calloc((size_t)dither_row_count * error_width, sizeof(color_delta));
    if (!state->color_error) {
        return OUT_OF_MEMORY_ERROR;
    }

    state->symbol_frequency = calloc(symbol_count, sizeof(uint32_t));
    if (!state->symbol_frequency) {
        return OUT_OF_MEMORY_ERROR;
    }

    for (uint_fast8_t i = 0; i < 5; i++) {
        state->original_frequency[i] = calloc(symbol_count, sizeof(uint32_t));
        if (!state->original_frequency[i]) {
            return OUT_OF_MEMORY_ERROR;
        }
    }

    for (uint_fast8_t filter = 0; filter < 5; filter++) {
        for (uint32_t y = 0; y < image->height; y++) {
            for (uint32_t x = 0; x < image->width; x++) {
                for (uint32_t c = 0; c < image->bytes_per_pixel; c++) {
                    uint32_t offset = x*image->bytes_per_pixel + c;
                    unsigned char color = image->rows[y][offset];
                    unsigned char left = 0;
                    if (x > 0) {
                        left = image->rows[y][offset-image->bytes_per_pixel];
                    }
                    unsigned char predicted = filter_predict(image, x, y, filter, c, left);
                    unsigned char filtered = color - predicted;
                    //fprintf(stderr, "color %d predicted %d filtered %d\n", (int)color, (int)predicted, (int)filtered);
                    state->original_frequency[filter][filtered]++;
                }
            }
        }
    }

    return SUCCESS;
}

void optimize_state_destroy(optimize_state *state) {
    free(state->pixels);
    free(state->color_error);
    free(state->symbol_frequency);
    for (uint_fast8_t filter = 0; filter < 5; filter++) {
        free(state->original_frequency[filter]);
    }
}

void optimize_state_copy(
    optimize_state *to,
    optimize_state *from,
    pngloss_image *image
) {
    to->x = from->x;
    to->y = from->y;

    memcpy(to->pixels, from->pixels, (size_t)image->width * image->bytes_per_pixel);

    uint32_t error_width = image->width + dither_filter_width;
    memcpy(to->color_error, from->color_error, (size_t)dither_row_count * error_width * sizeof(color_delta));

    memcpy(to->symbol_frequency, from->symbol_frequency, (size_t)symbol_count * sizeof(uint32_t));
    to->symbol_count = from->symbol_count;
}

void optimize_state_run(
    optimize_state *state, pngloss_image *image, pngloss_filter filter,
    uint_fast8_t quantization_strength, int_fast16_t bleed_divider
) {
    int_fast16_t back_color[4];
    int_fast16_t here_color[4];
    for (uint_fast8_t c = 0; c < image->bytes_per_pixel; c++) {
        uint32_t offset = state->x*image->bytes_per_pixel + c;
        back_color[c] = image->rows[state->y][offset];
        uint_fast8_t i = c;
        unsigned char left = 0;
        if (state->x > 0) {
            left = state->pixels[offset-image->bytes_per_pixel];
        }
        int_fast16_t predicted = filter_predict(image, state->x, state->y, filter, c, left);

        unsigned char best_symbol;
        if ((image->bytes_per_pixel % 2) == 0 && image->rows[state->y][state->x*image->bytes_per_pixel+image->bytes_per_pixel-1] == 0) {
            if (c == image->bytes_per_pixel-1) {
                // leave fully transparent pixels fully transparent, symbol
                // is expensive but artifacts are unacceptable otherwise
                here_color[c] = 0;
                back_color[c] = 0;
                best_symbol = 0 - predicted;
            } else {
                // color of fully transparent pixel doesn't matter and using
                // zero symbol to get predicted color minimizes file size
                here_color[c] = predicted;
                back_color[c] = predicted;
                best_symbol = 0;
            }
        } else {
            // convert from pixel index to color delta index
            if (image->bytes_per_pixel == 2 && c == 1) {
                // pixel alpha and color delta alpha are at different
                // indexes when colorspace is gray+alpha
                i = 3;
            }
            int_fast16_t color_error = state->color_error[state->x+dither_filter_width/2][i];
            here_color[c] = back_color[c] + color_error;

            int_fast16_t original = back_color[c] - predicted;
            if (original < -128) {
                predicted -= 256;
                original = back_color[c] - predicted;
            } else if (original > 127) {
                predicted += 256;
                original = back_color[c] - predicted;
            }
            int_fast16_t filtered = here_color[c] - predicted;

            // Find assigned band of values for filtered.
            int_fast16_t min, max;
            if (filtered < 0) {
                max = -(-filtered - (-filtered % (quantization_strength + 1)));
                min = max - quantization_strength;
            } else {
                min = filtered - (filtered % (quantization_strength + 1));
                max = min + quantization_strength;
            }
                
            if (min + predicted < 0) {
                min = 0 - predicted;
            }
            if (max + predicted > 255) {
                max = 255 - predicted;
            }
            if (max < min) {
                if (filtered + predicted > 255) {
                    min = 255 - predicted;
                    max = 255 - predicted;
                }
                if (filtered + predicted < 0) {
                    min = 0 - predicted;
                    max = 0 - predicted;
                }
            }

            bool found_best = false;
            uint32_t best_frequency = 0;
            for (int_fast16_t symbol = min; symbol <= max; symbol++) {
                int_fast16_t back = symbol + predicted;
                if (back < 0 || back > 255) {
                    fprintf(stderr, "back %d min %d max %d\n", (int)back, (int)min, (int)max);
                    abort();
                }
                bool new_best = false;
                uint32_t frequency = state->symbol_frequency[(unsigned char)symbol];

                if (!found_best) {
                    new_best = true;
                } else if (best_frequency < frequency) {
                    new_best = true;
                } else if (best_frequency == frequency) {
                    uint32_t best_close_freq = state->original_frequency[filter][best_symbol];
                    uint32_t close_freq = state->original_frequency[filter][(unsigned char)symbol];
                    if (best_close_freq < close_freq) {
                        new_best = true;
                    } else if (best_close_freq == close_freq) {
                        if (symbol == original) {
                            new_best = true;
                        }
                    }
                }
                if (new_best) {
                    found_best = true;
                    best_frequency = frequency;
                    best_symbol = symbol;
                    back_color[c] = back;
                }
            }
            if (!found_best) {
                fprintf(stderr, "color %d min %d max %d\n", (int)back_color[c], (int)min, (int)max);
                abort();
            }
        }

        state->pixels[offset] = back_color[c];

        state->symbol_frequency[best_symbol]++;
        state->symbol_count++;
    }

    color_delta difference;
    color_difference(image->bytes_per_pixel, difference, back_color, here_color);
    diffuse_color_error(state, image, difference, bleed_divider);

    state->x++;
}

uint32_t optimize_state_row(
    optimize_state *state, pngloss_image *image, pngloss_filter filter,
    uint_fast8_t quantization_strength,
    int_fast16_t bleed_divider, bool adaptive
) {
    while (state->x < image->width) {
        optimize_state_run(
            state,
            image,
            filter,
            quantization_strength,
            bleed_divider
        );
    }

    unsigned char *above_row = NULL;
    if (state->y > 0) {
        above_row = image->rows[state->y - 1];
    }

    if (adaptive) {
        uint_fast8_t adaptive_filter = adaptive_filter_for_rows(image, above_row, state->pixels);
        if (filter != adaptive_filter) {
            return -1;
        }
    }

    uint32_t total_cost = 0;
    for (uint32_t x = 0; x < image->width; x++) {
        for (uint_fast8_t c = 0; c < image->bytes_per_pixel; c++) {
            uint32_t offset = x * image->bytes_per_pixel + c;
            unsigned char left = 0;
            if (x > 0) {
                left = state->pixels[offset - image->bytes_per_pixel];
            }
            unsigned char predicted = filter_predict(image, x, state->y, filter, c, left);
            unsigned char symbol = state->pixels[offset] - predicted;
            uint32_t frequency = state->symbol_frequency[symbol];
            if (frequency) {
                uint_fast8_t cost = ulog2(UINTMAX_MAX / frequency);
                total_cost += cost;
            }
        }
    }

    // move color errors up one row
    uint32_t error_width = image->width + dither_filter_width;
    memmove(
        state->color_error,
        state->color_error + error_width,
        (dither_row_count - 1) * error_width * sizeof(color_delta)
    );
    memset(state->color_error + (dither_row_count - 1) * error_width, 0, error_width * sizeof(color_delta));

    // advance to next row and indicate success and cost to caller
    state->x = 0;
    state->y++;

    return total_cost;
}

unsigned char filter_predict(
    pngloss_image *image, uint32_t x, uint32_t y,
    pngloss_filter filter, uint_fast8_t c, unsigned char left
) {
    uint32_t offset = x*image->bytes_per_pixel + c;
    unsigned char above = 0, diag = 0;
    if (y > 0) {
        above = image->rows[y-1][offset];
        if (x > 0) {
            diag = image->rows[y-1][offset-image->bytes_per_pixel];
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
    optimize_state *state, pngloss_image *image,
    color_delta difference, int_fast16_t bleed_divider
) {
    uint32_t error_width = image->width + dither_filter_width;

    // hardcoded 4 instead of bytes_per_pixel because indexing color delta and not pixels
    for (uint_fast8_t c = 0; c < 4; c++) {
        int_fast16_t d = difference[c];

        // reduce color bleed
        d = d / bleed_divider;

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
        state->color_error[error_width * 2 + state->x + 2][c] += threes;

        int_fast16_t fours = d * 2/9;
        d -= fours * 2;
        state->color_error[error_width * 1 + state->x + 1][c] += fours;
        state->color_error[error_width * 1 + state->x + 3][c] += fours;

        int_fast16_t five = d / 2;
        d -= five;
        state->color_error[error_width * 1 + state->x + 2][c] += five;

        state->color_error[error_width * 0 + state->x + 3][c] += d;

        /*
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
        */
    }
}

uint_fast8_t adaptive_filter_for_rows(
    pngloss_image *image, unsigned char *above_row, unsigned char *pixels
) {
    uint32_t none_sum = 0, sub_sum = 0, up_sum = 0;
    uint32_t average_sum = 0, paeth_sum = 0;

    for (uint32_t i = 0; i < image->width * image->bytes_per_pixel; i++) {
        unsigned char above = 0, left = 0, diag = 0;
        if (i >= image->bytes_per_pixel) {
            left = pixels[i-image->bytes_per_pixel];
            if (above_row) {
                diag = above_row[i-image->bytes_per_pixel];
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
uint_fast8_t ulog2(uintmax_t x) {
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

