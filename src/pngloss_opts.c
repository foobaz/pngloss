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
    {"strength", required_argument, NULL, 's'},
    {"bleed", required_argument, NULL, 'b'},
    {NULL, 0, NULL, 0},
};

pngloss_error pngloss_parse_options(int argc, char *argv[], struct pngloss_options *options)
{
    int opt;
    do {
        char *strength_end;
        unsigned long strength;
        char *bleed_end;
        unsigned long bleed_divider;

        opt = getopt_long(argc, argv, "vqfo:Vhs:b:", long_options, NULL);
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

            /*
            case '1': case '2': case '3':
            case '4': case '5': case '6':
            case '7': case '8': case '9':
                options->level = opt - '0';
                break;
            */

            case 's':
                strength = strtoul(optarg, &strength_end, 10);
                if (strength_end != optarg && '\0' == strength_end[0]) {
                    options->strength = strength;
                } else {
                    fputs("-s, --strength requires a numeric argument\n", stderr);
                    return INVALID_ARGUMENT;
                }
                break;

            case 'b':
                bleed_divider = strtoul(optarg, &bleed_end, 10);
                if (bleed_end != optarg && '\0' == bleed_end[0]) {
                    options->bleed_divider = bleed_divider;
                } else {
                    fputs("-b, --bleed requires a numeric argument\n", stderr);
                    return INVALID_ARGUMENT;
                }
                break;

            case -1: break;

            default:
                return INVALID_ARGUMENT;
        }
    } while (opt != -1);

    int argn = optind;

    if (argn < argc) {
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
