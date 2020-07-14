#ifndef PNGQUANT_OPTS_H
#define PNGQUANT_OPTS_H

struct pngloss_options {
    const char *extension;
    const char *output_file_path;
    char *const *files;
    unsigned long strength;
    unsigned int level;
    unsigned int num_files;
    bool using_stdin, using_stdout, force,
        min_quality_limit, skip_if_larger, strip,
        print_help, print_version, missing_arguments,
        verbose;
};

pngloss_error pngloss_parse_options(int argc, char *argv[], struct pngloss_options *options);
#endif
