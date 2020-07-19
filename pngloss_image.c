/**
 © 2020 William MacKay.
 © 2013 Kornel Lesiński.
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

#include <png.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "optimize_state.h"
#include "pngloss_filters.h"
#include "pngloss_image.h"
#include "rwpng.h"

void optimizeForAverageFilter(
    unsigned char *pixels, int width, int height, int quantization_strength
) {
    //const uint_fast8_t bytes_per_pixel = 4;
    uint32_t stride = width * bytes_per_pixel;

    uint_fast16_t sliding_length = 40;
    const uint_fast8_t max_run_length = 14;
    optimize_with_stride(pixels, width, height, stride, sliding_length, max_run_length, quantization_strength, false);
}

void optimize_with_stride(
    unsigned char *pixels, uint32_t width, uint32_t height,
    uint32_t stride, uint_fast16_t sliding_length, uint_fast8_t max_run_length,
    uint_fast8_t quantization_strength, bool verbose
) {
    unsigned char **rows = malloc((size_t)height * sizeof(unsigned char *));
    for (uint32_t i = 0; i < height; i++) {
        rows[i] = pixels + i*stride;
    }
    optimize_with_rows(rows, width, height, sliding_length, max_run_length, quantization_strength, verbose);
    free(rows);
}

pngloss_error optimize_with_rows(
    unsigned char **rows, uint32_t width, uint32_t height,
    uint_fast16_t sliding_length, uint_fast8_t max_run_length,
    uint_fast8_t quantization_strength, bool verbose
) {
    pngloss_image image = {
        .rows = rows,
        .width = width,
        .height = height,
    };
    return optimize_image(&image, sliding_length, max_run_length, quantization_strength, verbose);
}

#define filter_count 5
pngloss_error optimize_image(
    pngloss_image *image, uint_fast16_t sliding_length,
    uint_fast8_t max_run_length, uint_fast8_t quantization_strength,
    bool verbose
) {
    optimize_state state, filter_states[filter_count];
    unsigned char filter_names[filter_count] = {
        PNG_FILTER_NONE,
        PNG_FILTER_SUB,
        PNG_FILTER_UP,
        PNG_FILTER_AVG,
        PNG_FILTER_PAETH,
    };
    unsigned char (*filter_functions[filter_count])(unsigned char, unsigned char, unsigned char) = {
        pngloss_filter_none,
        pngloss_filter_sub,
        pngloss_filter_up,
        pngloss_filter_average,
        pngloss_filter_paeth,
    };
    pngloss_error retval;
    unsigned char spinner[filter_count] = {'-', '/', '|', '\\', '-'};
    unsigned char *last_row_pixels;

    retval = optimize_state_init(
        &state, image, sliding_length
    );
    for (uint_fast8_t i = 0; i < filter_count; i++) {
        if (SUCCESS == retval) {
            retval = optimize_state_init(
                filter_states + i, image, sliding_length
            );
        }
    }
    if (SUCCESS == retval) {
        last_row_pixels = calloc((size_t)image->width, bytes_per_pixel);
        if (!last_row_pixels) {
            retval = OUT_OF_MEMORY_ERROR;
        }
    }
    if (SUCCESS == retval) {
        while (state.y < image->height) {
            uint32_t current_y = state.y;
            uint32_t best_cost = -1;
            uint_fast8_t best_index = 0;
            bool found_best = false;
            uint_fast8_t strength = quantization_strength;
            while (!found_best) {
                for (uint_fast8_t i = 0; i < filter_count; i++) {
                    // print progress display
                    float percent = 100.0f * (float)(current_y * 5 + i) / (float)(image->height * 5);
                    if (verbose) {
                        fprintf(stderr, "\x1B[\x01G%c %.1f%% complete", (int)spinner[i], percent);
                        fflush(stderr);
                    }

                    // get to work
                    optimize_state_copy(filter_states + i, &state, image, sliding_length);
                    uint32_t cost = optimize_state_row(
                        filter_states + i,
                        image,
                        last_row_pixels,
                        sliding_length,
                        max_run_length,
                        strength,
                        best_cost,
                        filter_names[i],
                        filter_functions[i]
                    );
                    //fprintf(stderr, "filter %d costs %u\n", (int)i, (unsigned int)cost);
                    if (best_cost > cost) {
                        best_cost = cost;
                        best_index = i;
                        found_best = true;
                    }
                }

                // If already at zero strength, can't try again, so fail.
                // This should be impossible but check anyway.
                if (!strength) {
                    fprintf(stderr, "\naborting because no good row at y == %d\n", (int)current_y);
                    abort();
                }

                // if no filter succeeds, try again at lower quantization strength
                strength--;
            }
            //fprintf(stderr, "best filter %d cost %u\n", (int)best_index, (unsigned int)best_cost);
            memcpy(
                image->rows[current_y],
                filter_states[best_index].pixels,
                image->width * bytes_per_pixel
            );
            memcpy(
                last_row_pixels,
                filter_states[best_index].pixels,
                image->width * bytes_per_pixel
            );
            optimize_state_copy(&state, filter_states + best_index, image, sliding_length);
        }
        // done with progress display, advance to next line for subsequent messages
        if (verbose) {
            fputs("\x1B[\x01G  compression complete\n", stderr);
        }
    }
    optimize_state_destroy(&state);
    for (uint_fast8_t i = 0; i < filter_count; i++) {
        optimize_state_destroy(filter_states + i);
    }
    free(last_row_pixels);

    return retval;
}

