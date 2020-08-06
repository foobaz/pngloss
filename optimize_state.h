#ifndef OPTIMIZE_STATE_H
#define OPTIMIZE_STATE_H

#include "color_delta.h"
#include "pngloss_image.h"
#include "rwpng.h"

// data structures
typedef struct {
    uint32_t x, y;
    unsigned char *pixels;
    color_delta *color_error;
    uint32_t *symbol_frequency;
    uintmax_t symbol_count;
    uint32_t *original_frequency[5];
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
    optimize_state *state, pngloss_image *image
);
void optimize_state_destroy(optimize_state *state);
void optimize_state_copy(
    optimize_state *to,
    optimize_state *from,
    pngloss_image *image
);
uintmax_t optimize_state_run(
    optimize_state *state,
    pngloss_image *image,
    unsigned char *last_row_pixels,
    pngloss_filter filter,
    uint_fast8_t quantization_strength,
    int_fast16_t bleed_divider
);
uintmax_t optimize_state_row(
    optimize_state *state,
    pngloss_image *image,
    unsigned char *last_row_pixels,
    pngloss_filter filter,
    uint_fast8_t quantization_strength,
    int_fast16_t bleed_divider,
    bool adaptive
);
unsigned char filter_predict(
    pngloss_image *image, uint32_t x, uint32_t y,
    pngloss_filter filter, uint_fast8_t c, unsigned char left
);
void diffuse_color_error(
    optimize_state *state, pngloss_image *image,
    color_delta difference, int_fast16_t bleed_divider
);
uint_fast8_t adaptive_filter_for_rows(
    pngloss_image *image, unsigned char *above_row, unsigned char *pixels
);

uint_fast8_t ulog2(uintmax_t x);

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
