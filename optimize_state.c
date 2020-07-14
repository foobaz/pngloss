#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "optimize_state.h"

const uint_fast8_t error_row_count = 2;
const uint_fast8_t filter_width = 5;

pngloss_error optimize_state_init(
    optimize_state *state, pngloss_image *image, uint_fast8_t sliding_length
) {
    state->x = 0;
    state->y = 0;
    state->sliding_index = 0;

    // clear values in case we return early and later free uninitialized pointers
    state->sliding_window = NULL;
    state->pixels = NULL;
    state->color_error = NULL;

    // multiply by two to leave space for saved duplicate copy
    state->sliding_window = calloc((size_t)sliding_length * 2, 1);
    if (!state->sliding_window) {
        return OUT_OF_MEMORY_ERROR;
    }

    // multiply by two to leave space for saved duplicate copy
    state->pixels = calloc((size_t)image->width * 2, image->bytes_per_pixel);
    if (!state->pixels) {
        return OUT_OF_MEMORY_ERROR;
    }

    uint32_t error_width = image->width + filter_width - 1;
    // multiply by two to leave space for saved duplicate copy
    state->color_error = calloc((size_t)error_row_count * error_width * 2, sizeof(color_delta));
    if (!state->color_error) {
        return OUT_OF_MEMORY_ERROR;
    }

    return SUCCESS;
}

void optimize_state_destroy(optimize_state *state) {
    free(state->sliding_window);
    free(state->pixels);
    free(state->color_error);
}

void optimize_state_copy(
    optimize_state *to,
    optimize_state *from,
    pngloss_image *image,
    uint_fast16_t sliding_length
) {
    to->x = from->x;
    to->y = from->y;

    memcpy(to->sliding_window, from->sliding_window, (size_t)sliding_length * 2);
    to->sliding_index = from->sliding_index;

    memcpy(to->pixels, from->pixels, (size_t)image->width * 2 * image->bytes_per_pixel);
    to->sliding_index = from->sliding_index;

    uint32_t error_width = image->width + filter_width - 1;
    memcpy(to->color_error, from->color_error, (size_t)error_row_count * error_width * 2 * sizeof(color_delta));
}

uint_fast8_t optimize_state_run(
    optimize_state *state, pngloss_image *image, uint_fast8_t sliding_length,
    uint_fast8_t max_run_length, uint_fast8_t quantization_strength,
    unsigned char (*filter)(
        unsigned char up, unsigned char diag, unsigned char left
    )
) {
    // Don't go farther than the end of the line because that's when we
    // compare the different filter types and decide which to keep.
    uint32_t until_line_end = image->width - state->x;
    if (max_run_length > until_line_end) {
        max_run_length = until_line_end;
    }

    // save copy of x to restore after failed match
    // not necessary to save y because won't span rows
    uint32_t saved_x = state->x;

    // save copy of sliding window to restore after failed match
    memcpy(state->sliding_window + sliding_length, state->sliding_window, sliding_length);
    uint32_t saved_sliding_index = state->sliding_index;
    uint32_t sliding_dirty_left = sliding_length - 1;
    uint32_t sliding_dirty_right = 0;


    // save copy of pixels to restore after failed match
    memcpy(
        state->pixels + image->width * image->bytes_per_pixel,
        state->pixels,
        image->width * image->bytes_per_pixel
    );
    uint32_t pixels_dirty_left = image->width - 1;
    uint32_t pixels_dirty_right = 0;

    uint32_t error_width = image->width + filter_width - 1;
    memcpy(
        state->color_error + error_row_count * error_width,
        state->color_error,
        error_row_count * error_width * sizeof(color_delta)
    );
    uint32_t color_dirty_left = error_width - 1;
    uint32_t color_dirty_right = 0;

    // start with best-case length and distance and back off until match found
    for (uint_fast8_t run_length = max_run_length; run_length > 0; run_length--) {
        for (uint_fast16_t distance = 1; distance < sliding_length; distance++) {
            bool match_failed = false;
            unsigned long max_error = (unsigned long)run_length * quantization_strength * quantization_strength;
            unsigned long total_error = 0;
            for (uint_fast8_t back_index = 0; back_index < run_length; back_index++) {
                unsigned char back_color[4];
                int_fast16_t here_color[4];
                //fprintf(stderr, "run_length == %d, distance == %d, x == %d, max_error == %d, total_error == %d, back_index == %d\n", (int)run_length, (int)distance, (int)state->x, (int)max_error, (int)total_error, (int)back_index);
                for (uint_fast8_t c = 0; c < image->bytes_per_pixel; c++) {
                    unsigned char back_value = state->sliding_window[(sliding_length + state->sliding_index - distance) % sliding_length];
                    state->sliding_window[state->sliding_index] = back_value;
                    if (sliding_dirty_left > state->sliding_index) {
                        sliding_dirty_left = state->sliding_index;
                    }
                    if (sliding_dirty_right < state->sliding_index) {
                        sliding_dirty_right = state->sliding_index;
                    }
                    state->sliding_index = (state->sliding_index + 1) % sliding_length;

                    uint32_t offset = state->x*image->bytes_per_pixel + c;
                    unsigned char above = 0, diag = 0, left = 0;
                    if (state->y > 0) {
                        above = image->rows[state->y-1][offset];
                        if (state->x > 0) {
                            diag = image->rows[state->y-1][offset-image->bytes_per_pixel];
                        }
                    }
                    if (state->x > 0) {
                        left = state->pixels[offset-image->bytes_per_pixel];
                    }
                    //back_color[c] = back_value + (above + left) / 2;
                    unsigned char filter_value = (*filter)(above, diag, left);
                    back_color[c] = back_value + filter_value;

                    here_color[c] = (int_fast16_t)image->rows[state->y][offset] + state->color_error[state->x+2][c];
                    state->pixels[offset] = back_color[c];
                }
                if (pixels_dirty_left > state->x) {
                    pixels_dirty_left = state->x;
                }
                if (pixels_dirty_right < state->x) {
                    pixels_dirty_right = state->x;
                }

                color_delta difference;
                color_difference(back_color, here_color, difference, image->bytes_per_pixel);
                uint32_t color_error = color_distance(difference);
                //fprintf(stderr, "In %s, total_error == %d, color_error == %d, difference == {%d, %d, %d, %d}\n", __func__, (int)total_error, (int)color_error, (int)difference[0], (int)difference[1], (int)difference[2], (int)difference[3]);
                total_error += (unsigned long)color_error;
                if (color_error > max_error || total_error > max_error) {
                    // too much error, abort this run length + distance and try another
                    match_failed = true;
                    //fprintf(stderr, "In %s, failed match with run_length == %d, average_error == %d, distance == %d, reverting to x == %d\n", __func__, (int)run_length, (int)(total_error / (state->x - saved_x)), (int)distance, (int)saved_x);
                    // restore saved x coordinate
                    state->x = saved_x;
                    // restore saved sliding window
                    state->sliding_index = saved_sliding_index;
                    if (!(sliding_dirty_left > sliding_dirty_right)) {
                        memcpy(
                            state->sliding_window + sliding_dirty_left,
                            state->sliding_window + sliding_length + sliding_dirty_left,
                            sliding_dirty_right - sliding_dirty_left + 1
                        );
                    }
                    sliding_dirty_left = sliding_length - 1;
                    sliding_dirty_right = 0;
                    if (!(pixels_dirty_left > pixels_dirty_right)) {
                        memcpy(
                            state->pixels + pixels_dirty_left * image->bytes_per_pixel,
                            state->pixels + (image->width + pixels_dirty_left) * image->bytes_per_pixel,
                            (pixels_dirty_right - pixels_dirty_left + 1) * image->bytes_per_pixel
                        );
                    }
                    pixels_dirty_left = image->width - 1;
                    pixels_dirty_right = 0;

                    if (!(color_dirty_left > color_dirty_right)) {
                        memcpy(
                            state->color_error + color_dirty_left,
                            state->color_error + error_row_count * error_width + color_dirty_left,
                            (color_dirty_right - color_dirty_left + 1) * sizeof(color_delta)
                        );
                    }
                    color_dirty_left = error_width - 1;
                    color_dirty_right = 0;
                    break;
                }

                for (uint_fast8_t c = 0; c < image->bytes_per_pixel; c++) {
                    // perform two-row sierra dithering
                    int_fast16_t rounding;
                    if (difference[c] >= 0) {
                        rounding = 8;
                    } else {
                        rounding = -8;
                    }
                    int_fast16_t ones = (difference[c]+rounding)/16;
                    difference[c] -= ones * 2;
                    state->color_error[error_width + state->x + 0][c] += ones;
                    state->color_error[error_width + state->x + 4][c] += ones;

                    if (difference[c] >= 0) {
                        rounding = 3;
                    } else {
                        rounding = -3;
                    }
                    int_fast16_t twos = (difference[c]+rounding)/7;
                    difference[c] -= twos * 2;
                    state->color_error[error_width + state->x + 1][c] += twos;
                    state->color_error[error_width + state->x + 3][c] += twos;

                    if (difference[c] >= 0) {
                        rounding = 2;
                    } else {
                        rounding = -2;
                    }
                    int_fast16_t threes = (difference[c]+rounding)*3/10;
                    difference[c] -= threes * 2;
                    state->color_error[error_width + state->x + 2][c] += threes;
                    state->color_error[state->x + 2][c] += threes;

                    int_fast16_t four = difference[c];
                    state->color_error[state->x + 3][c] += four;

                    uint32_t new_dirty_left = state->x;
                    uint32_t new_dirty_right = error_width + state->x + 4;
                    if (color_dirty_left > new_dirty_left) {
                        color_dirty_left = new_dirty_left;
                    }
                    if (color_dirty_right < new_dirty_right) {
                        color_dirty_right = new_dirty_right;
                    }
                }

                // looking good so far, continue to next pixel in run
                state->x++;
            }
            if (!match_failed) {
                // found matching run
                //fprintf(stderr, "Found match at %u, %u, run_length == %u, distance == %u\n", (unsigned int)state->x, (unsigned int)state->y, (unsigned int)run_length, (unsigned int)distance);
                uint_fast8_t log2 = 0;
                while (distance) {
                    distance >>= 1;
                    log2++;
                }
                if (log2 > 2) {
                    log2 -= 2;
                } else {
                    log2 = 0;
                }
                return 8 + log2;
            }
        }
    }
    // found no matching run, give up and insert pixel as-is (uncompressed)
    for (uint_fast8_t c = 0; c < image->bytes_per_pixel; c++) {
        uint32_t offset = state->x*image->bytes_per_pixel + c;
        unsigned char above = 0, diag = 0, left = 0;
        if (state->y > 0) {
            above = image->rows[state->y-1][offset];
            if (state->x > 0) {
                diag = image->rows[state->y-1][offset-image->bytes_per_pixel];
            }
        }
        if (state->x > 0) {
            left = state->pixels[offset-image->bytes_per_pixel];
        }

        int_fast16_t here_color = (int_fast16_t)image->rows[state->y][offset] + state->color_error[state->x+2][c];
        // TODO: propagate this saturation error instead of discarding it
        if (here_color < 0) {
            here_color = 0;
        }
        if (here_color > 255) {
            here_color = 255;
        }
        //unsigned char average_value = here_color - (above + left) / 2;
        unsigned char filtered_value = here_color - (*filter)(above, diag, left);
        state->pixels[offset] = here_color;

        //state->sliding_window[state->sliding_index] = average_value;
        state->sliding_window[state->sliding_index] = filtered_value;
        state->sliding_index = (state->sliding_index + 1) % sliding_length;
    }
    state->x++;
    //fprintf(stderr, "Inserting pixel at %u, %u, sliding_index == %u\n", (unsigned int)state->x, (unsigned int)state->y, (unsigned int)state->sliding_index);
    return 8;
}

uint32_t optimize_state_row(
    optimize_state *state, pngloss_image *image, uint_fast8_t sliding_length,
    uint_fast8_t max_run_length, uint_fast8_t quantization_strength,
    uint32_t best_cost, uint_fast8_t desired_filter,
    unsigned char (*filter)(
        unsigned char up, unsigned char diag, unsigned char left
    )
) {
    //fprintf(stderr, "In %s, width == %d, height == %d, sliding_length == %d, quantization == %d, bytes_per_pixel == %d\n", __func__, (int)width, (int)height, (int)sliding_length, (int)quantization_strength, (int)bytes_per_pixel);
    uint32_t total_cost = 0;
    while (state->x < image->width) {
        uint_fast8_t cost = optimize_state_run(state, image, sliding_length, max_run_length, quantization_strength, filter);
        total_cost += cost;
        //fprintf(stderr, "In %s, cost == %d, total_cost == %d\n", __func__, (int)cost, (int)total_cost);
        if (best_cost < total_cost) {
            //fprintf(stderr, "returning early, best_cost == %u, total_cost == %u\n", (unsigned int)best_cost, (unsigned int)total_cost);
            return -1;
        }
    }

    unsigned char *above_row = NULL;
    if (state->y > 0) {
        above_row = image->rows[state->y - 1];
    }
    uint_fast8_t adaptive_filter = adaptive_filter_for_rows(image, above_row, state->pixels);
    //fprintf(stderr, "finished row %u, cost == %lu, filter == 0x%X\n", (unsigned int)state->y, total_cost, (unsigned int)adaptive_filter);

    if (desired_filter != adaptive_filter) {
        return -1;
    }

    // advance to next row and indicate success and cost to caller
    state->x = 0;
    state->y++;

    uint32_t error_width = image->width + filter_width - 1;
    memmove(
        state->color_error,
        state->color_error + error_width,
        (error_row_count - 1) * error_width * sizeof(color_delta)
    );
    memset(state->color_error + (error_row_count - 1) * error_width, 0, error_width * sizeof(color_delta));
    return total_cost;
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
        return PNG_FILTER_NONE;
    }
    if (min_sum >= sub_sum) {
        return PNG_FILTER_SUB;
    }
    if (min_sum >= up_sum) {
        return PNG_FILTER_UP;
    }
    if (min_sum >= average_sum) {
        return PNG_FILTER_AVG;
    }
    if (min_sum >= paeth_sum) {
        return PNG_FILTER_PAETH;
    }
    // unreachable
    return PNG_NO_FILTERS;
}

