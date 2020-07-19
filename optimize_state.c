#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "optimize_state.h"

const uint_fast8_t error_row_count = 2;
const uint_fast8_t filter_width = 5;

pngloss_error optimize_state_init(
    optimize_state *state, pngloss_image *image, uint_fast16_t sliding_length
) {
    state->x = 0;
    state->y = 0;
    state->sliding_index = 0;

    // clear values in case we return early and later free uninitialized pointers
    state->sliding_window = NULL;
    state->pixels = NULL;
    state->color_error = NULL;

    // multiply by three to leave space for saved duplicate copy
    // and distance => run length cache
    state->sliding_window = calloc((size_t)sliding_length * 3, 1);
    if (!state->sliding_window) {
        return OUT_OF_MEMORY_ERROR;
    }

    // don't need saved duplicate for pixels
    state->pixels = calloc((size_t)image->width, bytes_per_pixel);
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

    memcpy(to->sliding_window, from->sliding_window, (size_t)sliding_length);
    to->sliding_index = from->sliding_index;

    memcpy(to->pixels, from->pixels, (size_t)image->width * bytes_per_pixel);

    uint32_t error_width = image->width + filter_width - 1;
    memcpy(to->color_error, from->color_error, (size_t)error_row_count * error_width * sizeof(color_delta));
}

uint_fast8_t optimize_state_run(
    optimize_state *state, pngloss_image *image, unsigned char *last_row_pixels,
    uint_fast16_t sliding_length, uint_fast8_t max_run_length,
    uint_fast8_t quantization_strength, unsigned char (*filter)(
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
    uint_fast16_t saved_sliding_index = state->sliding_index;
    uint_fast16_t sliding_dirty_left = sliding_length - 1;
    uint_fast16_t sliding_dirty_right = 0;

    // clear distance => run length cache
    memset(state->sliding_window + sliding_length * 2, 0xFF, sliding_length);

    // save copy of color error to restore after failed match
    uint32_t error_width = image->width + filter_width - 1;
    memcpy(
        state->color_error + error_row_count * error_width,
        state->color_error,
        error_row_count * error_width * sizeof(color_delta)
    );
    uint32_t color_top_dirty_left = error_width - 1;
    uint32_t color_top_dirty_right = 0;
    uint32_t color_bottom_dirty_left = error_width - 1;
    uint32_t color_bottom_dirty_right = 0;

    // don't look back before beginning of image
    unsigned long pixel_index = (unsigned long)state->y * image->width + state->x * bytes_per_pixel;
    uint_fast16_t max_distance = sliding_length;
    if (pixel_index < sliding_length) {
        max_distance = pixel_index;
    }

    uint32_t max_error = (uint32_t)bytes_per_pixel * quantization_strength * quantization_strength;

    // start with best-case length and distance and back off until match found
    for (uint_fast8_t run_length = max_run_length; run_length >= 3; run_length--) {
        //for (uint_fast16_t distance = bytes_per_pixel; distance < max_distance; distance += bytes_per_pixel) {
        for (uint_fast16_t distance = 1; distance < max_distance; distance++) {
            unsigned char failed_length = state->sliding_window[sliding_length * 2 + distance];
            if (run_length > failed_length) {
                // don't retry this distance again until run length drops
                // below previously failed threshold
                //fprintf(stderr, "\ndist %u run %u failed %u\n", (unsigned int)distance, (unsigned int)run_length, (unsigned int)failed_length);
                continue;
            }
            //fprintf(stderr, "len %u dist %u\n", (unsigned int)run_length, (unsigned int)distance);
            //unsigned long max_error = (unsigned long)run_length * bytes_per_pixel * quantization_strength * quantization_strength;
            //fprintf(stderr, "run_length == %u, max_error == %u\n", (unsigned int)run_length, (unsigned int)max_error);
            unsigned long total_error = 0;
            bool match_failed = false;

            for (uint_fast8_t back_index = 0; back_index < run_length; back_index++) {
                int_fast16_t back_color[4];
                int_fast16_t here_color[4];
                int_fast16_t old_above_color[4];
                int_fast16_t new_above_color[4];
                int_fast16_t old_left_color[4];
                int_fast16_t new_left_color[4];

                for (uint_fast8_t c = 0; c < bytes_per_pixel; c++) {
                    size_t sliding_back_index = ((size_t)sliding_length + state->sliding_index - distance) % sliding_length;
                    unsigned char back_value = state->sliding_window[sliding_back_index];
                    //fprintf(stderr, "len %u back %zu index %u\n", (unsigned int)sliding_length, sliding_back_index, (unsigned int)state->sliding_index);
                    state->sliding_window[state->sliding_index] = back_value;
                    if (sliding_dirty_left > state->sliding_index) {
                        sliding_dirty_left = state->sliding_index;
                    }
                    if (sliding_dirty_right < state->sliding_index) {
                        sliding_dirty_right = state->sliding_index;
                    }
                    //uint_fast16_t old_sliding_index = state->sliding_index;
                    state->sliding_index = (state->sliding_index + 1) % sliding_length;
                    //fprintf(stderr, "run inc'd index %u => %u\n", (unsigned int)old_sliding_index, (unsigned int)state->sliding_index);

                    uint32_t offset = state->x*bytes_per_pixel + c;
                    unsigned char above = 0, diag = 0, left = 0, old_left = 0;
                    if (state->y > 0) {
                        above = image->rows[state->y-1][offset];
                        if (state->x > 0) {
                            diag = image->rows[state->y-1][offset-bytes_per_pixel];
                        }
                    }
                    if (state->x > 0) {
                        left = state->pixels[offset-bytes_per_pixel];
                        old_left = image->rows[state->y][offset-bytes_per_pixel];
                    }
                    unsigned char filter_value = (*filter)(above, diag, left);
                    back_color[c] = (unsigned char)(back_value + filter_value);

                    here_color[c] = (int_fast16_t)image->rows[state->y][offset] + state->color_error[state->x+2][c];
                    old_above_color[c] = last_row_pixels[offset];
                    new_above_color[c] = above;
                    old_left_color[c] = old_left;
                    new_left_color[c] = left;
                }

                color_delta difference;
                color_difference(back_color, here_color, difference);
                uint32_t color_error = color_distance(difference);

                color_delta old_partial_above, new_partial_above;
                color_d2 d2_above;
                color_difference(here_color, old_above_color, old_partial_above);
                color_difference(back_color, new_above_color, new_partial_above);
                color_delta_difference(new_partial_above, old_partial_above, d2_above);
                uint32_t above_error = color_delta_distance(d2_above);

                color_delta old_partial_left, new_partial_left;
                color_d2 d2_left;
                color_difference(here_color, old_left_color, old_partial_left);
                color_difference(back_color, new_left_color, new_partial_left);
                color_delta_difference(new_partial_left, old_partial_left, d2_left);
                uint32_t left_error = color_delta_distance(d2_left);

                //fprintf(stderr, "error color %u above %u left %u\n", (unsigned int)color_error, (unsigned int)above_error, (unsigned int)left_error);
                total_error += color_error + above_error + left_error;

                if (max_error < color_error + above_error + left_error) {
                //if (color_error > max_error || total_error > max_error) {
                    // too much error, abort this run length + distance and try another
                    match_failed = true;
                    //fprintf(stderr, "failed match with run_length == %d, total_error == %d, average_error == %d, distance == %d\n", (int)run_length, (int)total_error, (int)(total_error / (state->x - saved_x + 1)), (int)distance);
                    //fprintf(stderr, "failed match\n");

                    // update distance => run length cache with failed match length
                    //unsigned char new_failed_length = 0xFF;
                    unsigned char new_failed_length = state->x - saved_x;
                    state->sliding_window[sliding_length * 2 + distance] = new_failed_length;

                    // restore saved x coordinate
                    state->x = saved_x;
                    // restore saved sliding window
                    //fprintf(stderr, "restoring index %u => %u\n", (unsigned int)state->sliding_index, (unsigned int)saved_sliding_index);
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

                    if (!(color_top_dirty_left > color_top_dirty_right)) {
                        memcpy(
                            state->color_error + color_top_dirty_left,
                            state->color_error + error_row_count * error_width + color_top_dirty_left,
                            (color_top_dirty_right - color_top_dirty_left + 1) * sizeof(color_delta)
                        );
                    }
                    color_top_dirty_left = error_width - 1;
                    color_top_dirty_right = 0;

                    if (!(color_bottom_dirty_left > color_bottom_dirty_right)) {
                        memcpy(
                            state->color_error + error_width + color_bottom_dirty_left,
                            state->color_error + error_row_count * error_width + error_width + color_bottom_dirty_left,
                            (color_bottom_dirty_right - color_bottom_dirty_left + 1) * sizeof(color_delta)
                        );
                    }
                    color_bottom_dirty_left = error_width - 1;
                    color_bottom_dirty_right = 0;

                    break;
                }
                //fprintf(stderr, "color %u d %u tot %u\n", (unsigned int)color_error, (unsigned int)derivative_error, (unsigned int)total_error);

                // passed color error check, now copy back_color to output pixels
                for (uint_fast8_t c = 0; c < bytes_per_pixel; c++) {
                    uint32_t offset = state->x*bytes_per_pixel + c;
                    state->pixels[offset] = back_color[c];
                }

                // diffuse color error from choice of current pixel to subsequent neighboring pixels
                diffuse_color_error(state, image, difference);

                uint32_t new_top_dirty_left = state->x + 3;
                uint32_t new_top_dirty_right = state->x + 4;
                if (color_top_dirty_left > new_top_dirty_left) {
                    color_top_dirty_left = new_top_dirty_left;
                }
                if (color_top_dirty_right < new_top_dirty_right) {
                    color_top_dirty_right = new_top_dirty_right;
                }

                uint32_t new_bottom_dirty_left = state->x;
                uint32_t new_bottom_dirty_right = state->x + 4;
                if (color_bottom_dirty_left > new_bottom_dirty_left) {
                    color_bottom_dirty_left = new_bottom_dirty_left;
                }
                if (color_bottom_dirty_right < new_bottom_dirty_right) {
                    color_bottom_dirty_right = new_bottom_dirty_right;
                }

                // looking good so far, continue to next pixel in run
                state->x++;
            }
            if (!match_failed) {
                // found matching run
                //fprintf(stderr, "match at %u, %u, run == %u, dist == %u\n", (unsigned int)state->x, (unsigned int)state->y, (unsigned int)run_length, (unsigned int)distance);
                //fprintf(stderr, "run == %u, dist == %u\n", (unsigned int)run_length, (unsigned int)distance);
                //fprintf(stderr, "%u\n", (unsigned int)run_length);
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
    int_fast16_t back_color[4];
    int_fast16_t here_color[4];
    for (uint_fast8_t c = 0; c < bytes_per_pixel; c++) {
        uint32_t offset = state->x*bytes_per_pixel + c;
        unsigned char above = 0, diag = 0, left = 0;
        if (state->y > 0) {
            above = image->rows[state->y-1][offset];
            if (state->x > 0) {
                diag = image->rows[state->y-1][offset-bytes_per_pixel];
            }
        }
        if (state->x > 0) {
            left = state->pixels[offset-bytes_per_pixel];
        }

        here_color[c] = (int_fast16_t)image->rows[state->y][offset] + state->color_error[state->x+2][c];
        if (here_color[c] < 0) {
            back_color[c] = 0;
        } else if (here_color[c] > 255) {
            back_color[c] = 255;
        } else {
            back_color[c] = here_color[c];
        }

        unsigned char filtered_value = (unsigned char)back_color[c] - (*filter)(above, diag, left);
        state->sliding_window[state->sliding_index] = filtered_value;
        //uint_fast16_t old_sliding_index = state->sliding_index;
        state->sliding_index = (state->sliding_index + 1) % sliding_length;
        //fprintf(stderr, "pixel inc'd index %u => %u\n", (unsigned int)old_sliding_index, (unsigned int)state->sliding_index);

        state->pixels[offset] = back_color[c];
    }

    color_delta difference;
    color_difference(back_color, here_color, difference);
    diffuse_color_error(state, image, difference);

    //fprintf(stderr, "Inserting pixel at %u, %u, sliding_index == %u\n", (unsigned int)state->x, (unsigned int)state->y, (unsigned int)state->sliding_index);
    state->x++;
    return 8;
}

uint32_t optimize_state_row(
    optimize_state *state, pngloss_image *image, unsigned char *last_row_pixels,
    uint_fast16_t sliding_length, uint_fast8_t max_run_length,
    uint_fast8_t quantization_strength, uint32_t best_cost,
    uint_fast8_t desired_filter, unsigned char (*filter)(
        unsigned char up, unsigned char diag, unsigned char left
    )
) {
    //fprintf(stderr, "In %s, width == %d, height == %d, sliding_length == %d, quantization == %d\n", __func__, (int)width, (int)height, (int)sliding_length, (int)quantization_strength);
    uint32_t total_cost = 0;
    while (state->x < image->width) {
        uint_fast8_t cost = optimize_state_run(
            state,
            image,
            last_row_pixels,
            sliding_length,
            max_run_length,
            quantization_strength,
            filter
        );
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

void diffuse_color_error(
    optimize_state *state, pngloss_image *image, color_delta difference
) {
    uint32_t error_width = image->width + filter_width - 1;

    for (uint_fast8_t c = 0; c < bytes_per_pixel; c++) {
        int_fast16_t d = difference[c];

        // perform two-row sierra dithering
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

