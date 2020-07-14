#ifndef PNGLOSS_FILTERS_H
#define PNGLOSS_FILTERS_H

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

#endif // PNGLOSS_FILTERS_H
