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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "optimize_state.h"
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

#define spin_count 4
pngloss_error optimize_image(
    pngloss_image *image, uint_fast16_t sliding_length,
    uint_fast8_t max_run_length, uint_fast8_t quantization_strength,
    bool verbose
) {
    optimize_state state, best, filter_state;
    pngloss_error retval;
    int spinner[spin_count] = {'-', '/', '|', '\\'};
    //wint_t spinner[spin_count] = {u'\u253C', u'\u251C', u'\u2514', u'\u2534', u'\u253C', u'\u252C', u'\u250C', u'\u251C', u'\u253C', u'\u2524', u'\u2510', u'\u252C', u'\u253C', u'\u2534', u'\u2518', u'\u2524'};

    uint_fast8_t spin_index = 0;

    retval = optimize_state_init(&state, image, sliding_length);
    if (SUCCESS == retval) {
        retval = optimize_state_init(&best, image, sliding_length);
    }
    if (SUCCESS == retval) {
        retval = optimize_state_init(&filter_state, image, sliding_length);
    }
    if (SUCCESS == retval) {
        struct timeval tp;
        time_t old_sec = 0;
        suseconds_t old_dsec = 0;
        while (state.y < image->height) {
            uint32_t current_y = state.y;
            uint32_t best_cost = -1;
            uint32_t best_strength = 0;
            uint_fast8_t best_filter = 0;
            bool found_best = false;
            uint_fast8_t strength = quantization_strength;
            while (!found_best) {
                for (pngloss_filter filter = 0; filter < pngloss_filter_count; filter++) {
                    if (verbose) {
                        // print progress display
                        int err;
                        err = gettimeofday(&tp, NULL);
                        if (err) {
                            spin_index = (spin_index + 1) % spin_count;
                        } else {
                            suseconds_t dsec = tp.tv_usec / 100000;
                            if (old_sec != tp.tv_sec || old_dsec != dsec) {
                                old_sec = tp.tv_sec;
                                old_dsec = dsec;
                                spin_index = (spin_index + 1) % spin_count;
                            }
                        }

                        uint_fast8_t progress = filter;
                        if (strength != quantization_strength) {
                            progress = pngloss_filter_count;
                        }
                        float percent = 100.0f * (float)(current_y * (pngloss_filter_count + 1) + progress) / (float)(image->height * (pngloss_filter_count + 1));

                        fprintf(stderr, "\x1B[\x01G%c %.1f%% complete", spinner[spin_index], percent);
                        fflush(stderr);
                    }

                    // get to work
                    optimize_state_copy(&filter_state, &state, image, sliding_length);
                    uint32_t cost = optimize_state_row(
                        &filter_state,
                        image,
                        sliding_length,
                        max_run_length,
                        strength,
                        best_cost,
                        filter
                    );
                    /*
                    fprintf(stderr, "filter %d costs %u\n", (int)i, (unsigned int)cost);
                    if (cost < (uint32_t)-1) {
                        fprintf(stderr, "filter %u costs %u\n", (unsigned int)i, (unsigned int)cost);
                    }
                    */

                    if (best_cost > cost) {
                        best_cost = cost;
                        best_filter = filter;
                        best_strength = strength;
                        found_best = true;
                        optimize_state_copy(&best, &filter_state, image, sliding_length);
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
            //fprintf(stderr, "row %u best cost %u filter %u strength %u\n", (unsigned int)current_y, (unsigned int)best_cost, (unsigned int)best_filter, (unsigned int)best_strength);
            memcpy(
                image->rows[current_y],
                best.pixels,
                image->width * bytes_per_pixel
            );
            optimize_state_copy(&state, &best, image, sliding_length);
        }
        // done with progress display, advance to next line for subsequent messages
        if (verbose) {
            fputs("\x1B[\x01G  compression complete\n", stderr);
        }
    }
    optimize_state_destroy(&state);
    optimize_state_destroy(&best);
    optimize_state_destroy(&filter_state);

    return retval;
}

