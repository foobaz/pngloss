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
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "optimize_state.h"
#include "pngloss_image.h"
#include "rwpng.h"

void optimizeForAverageFilter(
    unsigned char pixels[], int width, int height, int quantization_strength
) {
    const uint_fast8_t bytes_per_pixel = 4;
    uint32_t stride = width * bytes_per_pixel;
    // propagating half the color error is good middle ground
    const int_fast16_t bleed_divider = 2;

    optimize_with_stride(pixels, width, height, stride, false, quantization_strength, bleed_divider);
}

void optimize_with_stride(
    unsigned char *pixels, uint32_t width, uint32_t height, uint32_t stride,
    bool verbose, uint_fast8_t quantization_strength, int_fast16_t bleed_divider
) {
    unsigned char **rows = malloc((size_t)height * sizeof(unsigned char *));
    for (uint32_t i = 0; i < height; i++) {
        rows[i] = pixels + i*stride;
    }
    optimize_with_rows(rows, width, height, NULL, verbose, quantization_strength, bleed_divider);
    free(rows);
}

pngloss_error optimize_with_rows(
    unsigned char **rows, uint32_t width, uint32_t height,
    unsigned char *row_filters, bool verbose,
    uint_fast8_t quantization_strength, int_fast16_t bleed_divider
) {
    pngloss_error retval = SUCCESS;
    pngloss_image original_image = {
        .rows = rows,
        .width = width,
        .height = height,
        .bytes_per_pixel = 4
    };
    bool grayscale = true;
    bool strip_alpha = true;

    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            unsigned char *pixel = rows[y] + x*4;
            if (pixel[0] != pixel[1] || pixel[1] != pixel[2]) {
                grayscale = false;
            }
            if (pixel[3] < 255) {
                strip_alpha = false;
            }
        }
        if (!grayscale && !strip_alpha) {
            break;
        }
    }
    if (grayscale || strip_alpha) {
        pngloss_image image = {
            .width = width,
            .height = height
        };

        if (grayscale && strip_alpha) {
            image.bytes_per_pixel = 1;
        } else if (grayscale) {
            image.bytes_per_pixel = 2;
        } else {
            image.bytes_per_pixel = 3;
        }

        image.rows = malloc((size_t)height * sizeof(unsigned char **));
        unsigned char *pixels = malloc((size_t)height * width * image.bytes_per_pixel);

        if (!image.rows || !pixels) {
            retval = OUT_OF_MEMORY_ERROR;
        }

        // Copying to and from like this is not the most efficient, but it
        // shields the caller from worrying about pixel format and it's
        // much faster than performing the optimization.
        if (SUCCESS == retval) {
            for (uint32_t y = 0; y < height; y++) {
                image.rows[y] = pixels + (size_t)y * width * image.bytes_per_pixel;
                for (uint32_t x = 0; x < width; x++) {
                    unsigned char *original = rows[y] + (size_t)x*4;
                    unsigned char *pixel = image.rows[y] + (size_t)x*image.bytes_per_pixel;
                    if (grayscale && strip_alpha) {
                        pixel[0] = original[1];
                    } else if (grayscale) {
                        pixel[0] = original[1];
                        pixel[1] = original[3];
                    } else {
                        pixel[0] = original[0];
                        pixel[1] = original[1];
                        pixel[2] = original[2];
                    }
                }
            }
            retval = optimize_image(&image, row_filters, verbose, quantization_strength, bleed_divider);
        }
        if (SUCCESS == retval) {
            for (uint32_t y = 0; y < height; y++) {
                for (uint32_t x = 0; x < width; x++) {
                    unsigned char *original = rows[y] + (size_t)x*4;
                    unsigned char *pixel = image.rows[y] + (size_t)x*image.bytes_per_pixel;
                    if (grayscale && strip_alpha) {
                        original[0] = pixel[0];
                        original[1] = pixel[0];
                        original[2] = pixel[0];
                        original[3] = 255;
                    } else if (grayscale) {
                        original[0] = pixel[0];
                        original[1] = pixel[0];
                        original[2] = pixel[0];
                        original[3] = pixel[1];
                    } else {
                        original[0] = pixel[0];
                        original[1] = pixel[1];
                        original[2] = pixel[2];
                        original[3] = 255;
                    }
                }
            }
        }
        free(pixels);
        free(image.rows);
    } else {
        retval = optimize_image(&original_image, row_filters, verbose, quantization_strength, bleed_divider);
    }

    return retval;
}

#define spin_count 4
pngloss_error optimize_image(
    pngloss_image *image, unsigned char *row_filters, bool verbose,
    uint_fast8_t quantization_strength, int_fast16_t bleed_divider
) {
    pngloss_error retval;
    int spinner[spin_count] = {'-', '/', '|', '\\'};
    uint_fast8_t spin_index = 0;

    optimize_state state = {
        .pixels = NULL,
        .color_error = NULL,
        .symbol_frequency = NULL
    };
    retval = optimize_state_init(&state, image);

    optimize_state best = {
        .pixels = NULL,
        .color_error = NULL,
        .symbol_frequency = NULL
    };
    if (SUCCESS == retval) {
        retval = optimize_state_init(&best, image);
    }

    optimize_state filter_state = {
        .pixels = NULL,
        .color_error = NULL,
        .symbol_frequency = NULL
    };
    if (SUCCESS == retval) {
        retval = optimize_state_init(&filter_state, image);
    }

    unsigned char *last_row_pixels = NULL;
    if (SUCCESS == retval) {
        last_row_pixels = calloc((size_t)image->width, image->bytes_per_pixel);
    }

    if (SUCCESS == retval) {
        struct timeval tp;
        time_t old_sec = 0;
        suseconds_t old_dsec = 0;
        while (state.y < image->height) {
            uint32_t current_y = state.y;
            uintmax_t best_cost = UINTMAX_MAX;
            uint_fast8_t best_strength = 0;
            uint_fast8_t best_filter = 0;
            bool found_best = false;
            uint_fast8_t strength = quantization_strength;
            // PNG spec section 5.9 says,
            // "the first row must always be adaptively filtered"
            bool adaptive = (!row_filters || !current_y);
            while (!found_best) {
            //for (uint_fast8_t strength = 0; strength <= quantization_strength; strength++)
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
                    optimize_state_copy(&filter_state, &state, image);
                    uintmax_t cost = optimize_state_row(
                        &filter_state,
                        image,
                        last_row_pixels,
                        filter,
                        strength,
                        bleed_divider,
                        adaptive
                    );
                    /*
                    fprintf(stderr, "filter %u costs %lu\n", (unsigned int)filter, (unsigned long)cost);
                    if (cost < (uint32_t)-1) {
                        fprintf(stderr, "filter %u costs %u\n", (unsigned int)filter, (unsigned int)cost);
                    }
                    */

                    if (best_cost > cost) {
                        best_cost = cost;
                        best_filter = filter;
                        best_strength = strength;
                        found_best = true;
                        optimize_state_copy(&best, &filter_state, image);
                    }
                }

                // If already at zero strength, can't try again, so fail.
                // This should be impossible but check anyway.
                if (!found_best && !strength) {
                    fprintf(stderr, "\naborting because no good row at y == %d\n", (int)current_y);
                    abort();
                }

                // if no filter succeeds, try again at lower quantization strength
                strength--;
            }
            //fprintf(stderr, "row %u best cost %u filter %u strength %u\n", (unsigned int)current_y, (unsigned int)best_cost, (unsigned int)best_filter, (unsigned int)best_strength);
            memcpy(
                last_row_pixels,
                image->rows[current_y],
                image->width * image->bytes_per_pixel
            );
            memcpy(
                image->rows[current_y],
                best.pixels,
                image->width * image->bytes_per_pixel
            );
            optimize_state_copy(&state, &best, image);
            if (row_filters) {
                unsigned char best_png_filter;
                switch (best_filter) {
                case pngloss_none:
                    best_png_filter = PNG_FILTER_NONE;
                    break;
                case pngloss_sub:
                    best_png_filter = PNG_FILTER_SUB;
                    break;
                case pngloss_up:
                    best_png_filter = PNG_FILTER_UP;
                    break;
                case pngloss_average:
                    best_png_filter = PNG_FILTER_AVG;
                    break;
                case pngloss_paeth:
                    best_png_filter = PNG_FILTER_PAETH;
                    break;
                }
                row_filters[current_y] = best_png_filter;
            }
        }
        // done with progress display, advance to next line for subsequent messages
        if (verbose) {
            fputs("\x1B[\x01G  compression complete\n", stderr);
        }
    }
    if (verbose) {
        unsigned int used_symbols = 0;
        for (uint_fast16_t i = 0; i < 256; i++) {
            uint32_t frequency = best.symbol_frequency[i];
            if (frequency) {
                //fprintf(stderr, "  %3u %u\n", (unsigned int)i, (unsigned int)frequency);
                used_symbols++;
            }
        }
        fprintf(stderr, "  used %u unique symbols\n", used_symbols++);
    }

    optimize_state_destroy(&state);
    optimize_state_destroy(&best);
    optimize_state_destroy(&filter_state);
    free(last_row_pixels);

    return retval;
}

