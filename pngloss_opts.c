/*
** © 2020 by William MacKay.
** © 2017 by Kornel Lesiński.
**
** See COPYRIGHT file for license.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <getopt.h>

#include "rwpng.h"
#include "pngloss_opts.h"

extern char *optarg;
extern int optind, opterr;

enum {arg_ext, arg_no_force, arg_skip_larger, arg_strip};

static const struct option long_options[] = {
    {"verbose", no_argument, NULL, 'v'},
    {"quiet", no_argument, NULL, 'q'},
    {"force", no_argument, NULL, 'f'},
    {"no-force", no_argument, NULL, arg_no_force},
    {"ext", required_argument, NULL, arg_ext},
    {"skip-if-larger", no_argument, NULL, arg_skip_larger},
    {"output", required_argument, NULL, 'o'},
    {"strip", no_argument, NULL, arg_strip},
    {"version", no_argument, NULL, 'V'},
    {"help", no_argument, NULL, 'h'},
    {NULL, 0, NULL, 0},
};

pngloss_error pngloss_parse_options(int argc, char *argv[], struct pngloss_options *options)
{
    int opt;
    do {
        opt = getopt_long(argc, argv, "Vvqfhs:Q:o:", long_options, NULL);
        switch (opt) {
            case 'v':
                options->verbose = true;
                break;
            case 'q':
                options->verbose = false;
                break;

            case 'f': options->force = true; break;
            case arg_no_force: options->force = false; break;

            case arg_ext: options->extension = optarg; break;
            case 'o':
                if (options->output_file_path) {
                    fputs("--output option can be used only once\n", stderr);
                    return INVALID_ARGUMENT;
                }
                if (strcmp(optarg, "-") == 0) {
                    options->using_stdout = true;
                    break;
                }
                options->output_file_path = optarg; break;

            case arg_skip_larger:
                options->skip_if_larger = true;
                break;

            case arg_strip:
                options->strip = true;
                break;

            case 'h':
                options->print_help = true;
                break;

            case 'V':
                options->print_version = true;
                break;

            case -1: break;

            default:
                return INVALID_ARGUMENT;
        }
    } while (opt != -1);

    int argn = optind;

    if (argn < argc) {
        char *quality_end;
        unsigned long quality = strtoul(argv[argn], &quality_end, 10);
        if (quality_end != argv[argn] && '\0' == quality_end[0]) {
            options->quality = quality;
            argn++;
        }

        if (argn == argc || (argn == argc-1 && 0==strcmp(argv[argn],"-"))) {
            options->using_stdin = true;
            options->using_stdout = !options->output_file_path;
            argn = argc-1;
        }

        options->num_files = argc-argn;
        options->files = argv+argn;
    } else if (argn <= 1) {
        options->missing_arguments = true;
    }

    return SUCCESS;
}
