#ifndef PNGLOSS_IMAGE_H
#define PNGLOSS_IMAGE_H

#include "rwpng.h"

// data structures
typedef struct {
    unsigned char **rows;
    uint32_t width, height;
    uint_fast8_t bytes_per_pixel;
} pngloss_image;

// function prototypes
void optimizeForAverageFilter(
    unsigned char pixels[], int width, int height, int quantization
);
void optimize_with_stride(
    unsigned char *pixels, uint32_t width, uint32_t height, uint32_t stride,
    bool verbose, uint_fast8_t quantization_strength, int_fast16_t bleed_divider
);
pngloss_error optimize_with_rows(
    unsigned char **rows, uint32_t width, uint32_t height,
    unsigned char *row_filters, bool verbose,
    uint_fast8_t quantization_strength, int_fast16_t bleed_divider
);
pngloss_error optimize_image(
    pngloss_image *image, unsigned char *row_filters, bool verbose,
    uint_fast8_t quantization_strength, int_fast16_t bleed_divider
);

#endif // PNGLOSS_IMAGE_H
