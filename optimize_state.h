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

typedef enum {
    pngloss_none,
    pngloss_sub,
    pngloss_up,
    pngloss_average,
    pngloss_paeth,
    pngloss_filter_count
} pngloss_filter;

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
    uint_fast8_t quantization, pngloss_filter filter
);
uint32_t optimize_state_row(
    optimize_state *state, pngloss_image *image, unsigned char *last_row_pixels,
    uint_fast16_t sliding_length, uint_fast8_t max_run_length,
    uint_fast8_t quantization_strength, uint32_t best_cost,
    pngloss_filter filter
);

void diffuse_color_error(
    optimize_state *state, pngloss_image *image, color_delta difference
);
uint_fast8_t adaptive_filter_for_rows(
    pngloss_image *image, unsigned char *above_row, unsigned char *pixels
);

uint_fast8_t ulog2(unsigned long x);

unsigned char pngloss_filter_none(
    unsigned char above, unsigned char diag, unsigned char left
);
unsigned char pngloss_filter_sub(
    unsigned char above, unsigned char diag, unsigned char left
);
unsigned char pngloss_filter_up(
    unsigned char above, unsigned char diag, unsigned char left
);
unsigned char pngloss_filter_average(
    unsigned char above, unsigned char diag, unsigned char left
);
unsigned char pngloss_filter_paeth(
    unsigned char above, unsigned char diag, unsigned char left
);

#endif // OPTIMIZE_STATE_H
