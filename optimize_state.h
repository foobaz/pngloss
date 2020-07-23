#ifndef OPTIMIZE_STATE_H
#define OPTIMIZE_STATE_H

#include "color_delta.h"
#include "pngloss_image.h"
#include "rwpng.h"

// data structures
typedef struct {
    uint32_t x, y;
    unsigned char *sliding_window;
    uint_fast16_t sliding_index;
    unsigned char *pixels;
    color_delta *color_error;
    uint32_t *symbol_frequency;
    unsigned long symbol_count;
} optimize_state;

// function prototypes
pngloss_error optimize_state_init(
    optimize_state *state, pngloss_image *image, uint_fast16_t sliding_length
);
void optimize_state_destroy(optimize_state *state);
void optimize_state_copy(
    optimize_state *to,
    optimize_state *from,
    pngloss_image *image,
    uint_fast16_t sliding_length
);
uint_fast8_t optimize_state_run(
    optimize_state *state, pngloss_image *image, unsigned char *last_row_pixels,
    uint_fast16_t sliding_length, uint_fast8_t max_run_length,
    uint_fast8_t quantization, unsigned char (*filter)(
        unsigned char up, unsigned char diag, unsigned char left
    )
);
uint32_t optimize_state_row(
    optimize_state *state, pngloss_image *image, unsigned char *last_row_pixels,
    uint_fast16_t sliding_length, uint_fast8_t max_run_length,
    uint_fast8_t quantization_strength, uint32_t best_cost,
    uint_fast8_t desired_filter, unsigned char (*filter)(
        unsigned char up, unsigned char diag, unsigned char left
    )
);
void diffuse_color_error(
    optimize_state *state, pngloss_image *image, color_delta difference
);
uint_fast8_t adaptive_filter_for_rows(
    pngloss_image *image, unsigned char *above_row, unsigned char *pixels
);

uint_fast8_t ulog2(unsigned long x);

#endif // OPTIMIZE_STATE_H
