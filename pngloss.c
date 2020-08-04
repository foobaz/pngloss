/* pngloss.c - lossily compress a png by using more compressible colors
**
** © 2020 by William MacKay
** © 2009-2019 by Kornel Lesiński.
** based on an algorithm by Michael Vinther
**
** See COPYRIGHT file for license.
*/

#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) || defined(WIN32) || defined(__WIN32__)
#  include <fcntl.h>    /* O_BINARY */
#  include <io.h>   /* setmode() */
#else
#  include <unistd.h>
#endif

#ifdef _OPENMP
#include <omp.h>
#else
#define omp_get_max_threads() 1
#define omp_get_thread_num() 0
#endif

#include "pngloss_image.h"
#include "pngloss_opts.h"
#include "rwpng.h"  /* typedefs, common macros, public prototypes */

char *PNGLOSS_USAGE = "\
usage:  pngloss [options] -- pngfile [pngfile ...]\n\
        pngloss [options] - >stdout <stdin\n\n\
options:\n\
  -s, --strength 26 how much quality to sacrifice, from 0 to 100 (default 26)\n\
  -b, --bleed 2     bleed divider, from 1 (full dithering) to 32767 (none)\n\
  -f, --force       overwrite existing output files\n\
  -o, --output file destination file path to use instead of --ext\n\
  -v, --verbose     print status messages\n\
  -q, --quiet       don't print status messages (default, overrides -v)\n\
  -V, --version     print version number\n\
  --skip-if-larger  only save converted files if they're smaller than original\n\
  --ext new.png     set custom suffix/extension for output filenames\n\
  --strip           remove optional metadata (default on Mac)\n\
\n\
Lossily compresses a PNG by using more compressible colors that are\n\
close enough to the original color values. The threshold determining\n\
what is close enough is controlled by the strength parameter. The output\n\
filename is the same as the input name except that it ends in \"-loss.png\"\n\
or your custom extension (unless the input is stdin, in which case the\n\
compressed image will go to stdout).  If you pass the special output path\n\
\"-\" and a single input file, that file will be processed and the\n\
compressed image will go to stdout. The default behavior if the output\n\
file exists is to skip the conversion; use --force to overwrite.\n";

char *PNGLOSS_VERSION = "0.6";

static pngloss_error prepare_output_image(png24_image *input_image, rwpng_color_transform tag, png24_image *output_image);
static pngloss_error read_image(const char *filename, bool using_stdin, png24_image *input_image_p, bool strip, bool verbose);
static pngloss_error write_image(png24_image *output_image24, unsigned char *row_filters, const char *outname, struct pngloss_options *options);
static char *add_filename_extension(const char *filename, const char *newext);
static bool file_exists(const char *outname);

void pngloss_internal_print_config(FILE *fd) {
    fputs(""
        #ifndef NDEBUG
                    "   WARNING: this is a DEBUG (slow) version.\n" /* NDEBUG disables assert() */
        #endif
        #if !USE_SSE && (defined(__SSE__) || defined(__amd64__) || defined(__X86_64__) || defined(__i386__))
                    "   SSE acceleration disabled.\n"
        #endif
        #if _OPENMP
                    "   Compiled with OpenMP (multicore support).\n"
        #endif
    , fd);
    fflush(fd);
}

FILE *pngloss_c_stderr() {
    return stderr;
}
FILE *pngloss_c_stdout() {
    return stdout;
}

static void print_full_version(FILE *fd)
{
    fprintf(fd, "pngloss, %s, by William MacKay, Kornel Lesinski.\n", PNGLOSS_VERSION);
    pngloss_internal_print_config(fd);
    rwpng_version_info(fd);
    fputs("\n", fd);
}

static void print_usage(FILE *fd)
{
    fputs(PNGLOSS_USAGE, fd);
}

pngloss_error pngloss_main_internal(struct pngloss_options *options);
static pngloss_error pngloss_file_internal(const char *filename, const char *outname, struct pngloss_options *options);

#ifndef PNGLOSS_NO_MAIN
int main(int argc, char *argv[])
{
    struct pngloss_options options = {
        .strength = 26,
        .bleed_divider = 2
    };

    pngloss_error retval = pngloss_parse_options(argc, argv, &options);
    if (retval != SUCCESS) {
        return retval;
    }

    if (options.print_version) {
        puts(PNGLOSS_VERSION);
        return SUCCESS;
    }

    if (options.missing_arguments) {
        print_full_version(stderr);
        print_usage(stderr);
        return MISSING_ARGUMENT;
    }

    if (options.print_help) {
        print_full_version(stdout);
        print_usage(stdout);
        return SUCCESS;
    }

    if (options.strength > 255) {
        fputs("Must specify a strength in the range 0-255.\n", stderr);
        return INVALID_ARGUMENT;
    }

    if (options.bleed_divider < 1 || options.bleed_divider > 32767) {
        fputs("Must specify a bleed divider in the range 1-32767.\n", stderr);
        return INVALID_ARGUMENT;
    }

    if (options.extension && options.output_file_path) {
        fputs("--ext and --output options can't be used at the same time\n", stderr);
        return INVALID_ARGUMENT;
    }

    // new filename extension depends on options used. Typically basename-fs8.png
    if (options.extension == NULL) {
        options.extension = "-loss.png";
    }

    if (options.output_file_path && options.num_files != 1) {
        fputs("  error: Only one input file is allowed when --output is used. This error also happens when filenames with spaces are not in quotes.\n", stderr);
        return INVALID_ARGUMENT;
    }

    if (options.using_stdout && !options.using_stdin && options.num_files != 1) {
        fputs("  error: Only one input file is allowed when using the special output path \"-\" to write to stdout. This error also happens when filenames with spaces are not in quotes.\n", stderr);
        return INVALID_ARGUMENT;
    }

    if (!options.num_files && !options.using_stdin) {
        fputs("No input files specified.\n", stderr);
        if (options.verbose) {
            print_full_version(stderr);
        }
        print_usage(stderr);
        return MISSING_ARGUMENT;
    }

    retval = pngloss_main_internal(&options);
    return retval;
}
#endif

// Don't use this. This is not a public API.
pngloss_error pngloss_main_internal(struct pngloss_options *options)
{
#ifdef _OPENMP
    // if there's a lot of files, coarse parallelism can be used
    if (options->num_files > 2*omp_get_max_threads()) {
        omp_set_nested(0);
        omp_set_dynamic(1);
    } else {
        omp_set_nested(1);
    }
#endif

    unsigned int error_count = 0, skipped_count = 0, file_count = 0;
    pngloss_error latest_error = SUCCESS;

    #pragma omp parallel for \
        schedule(static, 1) reduction(+:skipped_count) reduction(+:error_count) reduction(+:file_count) shared(latest_error)
    for (unsigned int i = 0; i < options->num_files; i++) {
        const char *filename = options->using_stdin ? "stdin" : options->files[i];
        struct pngloss_options opts = *options;


        #ifdef _OPENMP
        struct buffered_log buf = {0};
        if (opts.log_callback && omp_get_num_threads() > 1 && opts.num_files > 1) {
            liq_set_log_callback(local_liq, log_callback_buferred, &buf);
            liq_set_log_flush_callback(local_liq, log_callback_buferred_flush, &buf);
            opts.log_callback = log_callback_buferred;
            opts.log_callback_user_info = &buf;
        }
        #endif


        pngloss_error retval = SUCCESS;

        const char *outname = opts.output_file_path;
        char *outname_free = NULL;
        if (!opts.using_stdout) {
            if (!outname) {
                outname = outname_free = add_filename_extension(filename, opts.extension);
            }
            if (!opts.force && file_exists(outname)) {
                fprintf(stderr, "  error: '%s' exists; not overwriting\n", outname);
                retval = NOT_OVERWRITING_ERROR;
            }
        }

        if (SUCCESS == retval) {
            retval = pngloss_file_internal(filename, outname, &opts);
        }

        free(outname_free);

        if (retval) {
            #pragma omp critical
            {
                latest_error = retval;
            }
            if (retval == TOO_LOW_QUALITY || retval == TOO_LARGE_FILE) {
                skipped_count++;
            } else {
                error_count++;
            }
        }
        ++file_count;
    }

    if (options->verbose) {
        if (error_count) {
            fprintf(stderr, "There were errors compressing %d file%s out of a total of %d file%s.\n",
                           error_count, (error_count == 1)? "" : "s", file_count, (file_count == 1)? "" : "s");
        }
        if (skipped_count) {
            fprintf(stderr, "Skipped %d file%s out of a total of %d file%s.\n",
                           skipped_count, (skipped_count == 1)? "" : "s", file_count, (file_count == 1)? "" : "s");
        }
        if (!skipped_count && !error_count) {
            fprintf(stderr, "Compressed %d image%s.\n",
                           file_count, (file_count == 1)? "" : "s");
        }
    }

    return latest_error;
}

// I hacked it.
static pngloss_error pngloss_file_internal(const char *filename, const char *outname, struct pngloss_options *options) {
    pngloss_error retval = SUCCESS;

    if (options->verbose) {
        fprintf(stderr, "%s:\n", filename);
    }

    png24_image input_image = {.width=0};
    if (SUCCESS == retval) {
        retval = read_image(filename, options->using_stdin, &input_image, options->strip, options->verbose);
    }

    if (SUCCESS == retval && options->verbose) {
        fprintf(stderr, "  read %luKB file\n", (input_image.file_size+500UL)/1000UL);

        if (RWPNG_ICCP == input_image.input_color) {
            fprintf(stderr, "  used embedded ICC profile to transform image to sRGB colorspace\n");
        } else if (RWPNG_GAMA_CHRM == input_image.input_color) {
            fprintf(stderr, "  used gAMA and cHRM chunks to transform image to sRGB colorspace\n");
        } else if (RWPNG_ICCP_WARN_GRAY == input_image.input_color) {
            fprintf(stderr, "  warning: ignored ICC profile in GRAY colorspace\n");
        } else if (RWPNG_COCOA == input_image.input_color) {
            // No comment
        } else if (RWPNG_SRGB == input_image.input_color) {
            fprintf(stderr, "  passing sRGB tag from the input\n");
        } else if (input_image.gamma != 0.45455) {
            fprintf(stderr, "  converted image from gamma %2.1f to gamma 2.2\n",
                           1.0/input_image.gamma);
        }
    }

    png24_image output_image = {.width=0};
    if (SUCCESS == retval) {
        retval = prepare_output_image(&input_image, input_image.output_color, &output_image);
    }

    // not necessary to check return value because NULL row_filters is valid
    unsigned char *row_filters = malloc(input_image.height);

    if (SUCCESS == retval) {
        optimize_with_rows(output_image.row_pointers, output_image.width, output_image.height, row_filters, options->verbose, options->strength, options->bleed_divider);

        if (options->skip_if_larger) {
            output_image.maximum_file_size = input_image.file_size - 1;
        }

        output_image.chunks = input_image.chunks; input_image.chunks = NULL;
        retval = write_image(&output_image, row_filters, outname, options);

        if (options->verbose) {
            if (SUCCESS == retval) {
                unsigned long kb = ((unsigned long)output_image.file_size + 500UL) / 1000UL;
                float percent = 100.0f * (float)output_image.file_size / (float)input_image.file_size;
                fprintf(stderr, "  wrote %luKB file (%.1f%% of original)\n", kb, percent);
                if (output_image.metadata_size > 0) {
                    fprintf(stderr, "  copied %dKB of additional PNG metadata\n", (int)(output_image.metadata_size+500)/1000);
                }
            } else if (TOO_LARGE_FILE == retval) {
                unsigned long kb = ((unsigned long)output_image.maximum_file_size + 500UL) / 1000UL;
                fprintf(stderr, "  file exceeded maximum size of %luKB\n", kb);
            }
        }
    }

    if (options->using_stdout && (TOO_LARGE_FILE == retval || TOO_LOW_QUALITY == retval)) {
        // when outputting to stdout it'd be nasty to create 0-byte file
        // so if quality is too low, output 24-bit original
        pngloss_error write_retval = write_image(&input_image, NULL, outname, options);
        if (write_retval) {
            retval = write_retval;
        }
    }

    rwpng_free_image24(&input_image);
    rwpng_free_image24(&output_image);
    free(row_filters);

    return retval;
}

static bool file_exists(const char *outname)
{
    FILE *outfile = fopen(outname, "rb");
    if ((outfile ) != NULL) {
        fclose(outfile);
        return true;
    }
    return false;
}

/* build the output filename from the input name by inserting "-fs8" or
 * "-or8" before the ".png" extension (or by appending that plus ".png" if
 * there isn't any extension), then make sure it doesn't exist already */
static char *add_filename_extension(const char *filename, const char *newext)
{
    size_t x = strlen(filename);

    char* outname = malloc(x+4+strlen(newext)+1);
    if (!outname) return NULL;

    strcpy(outname, filename);
    if (x > 4 && (strncmp(outname+x-4, ".png", 4) == 0 || strncmp(outname+x-4, ".PNG", 4) == 0)) {
        strcpy(outname+x-4, newext);
    } else {
        strcpy(outname+x, newext);
    }

    return outname;
}

static char *temp_filename(const char *basename) {
    size_t x = strlen(basename);

    char *outname = malloc(x+1+4);
    if (!outname) return NULL;

    strcpy(outname, basename);
    strcpy(outname+x, ".tmp");

    return outname;
}

static void set_binary_mode(FILE *fp)
{
#if defined(_WIN32) || defined(WIN32) || defined(__WIN32__)
    setmode(fp == stdout ? 1 : 0, O_BINARY);
#else
#pragma unused(fp)
#endif
}

static const char *filename_part(const char *path)
{
    const char *outfilename = strrchr(path, '/');
    if (outfilename) {
        return outfilename+1;
    } else {
        return path;
    }
}

static bool replace_file(const char *from, const char *to, const bool force) {
#if defined(_WIN32) || defined(WIN32) || defined(__WIN32__)
    if (force) {
        // On Windows rename doesn't replace
        unlink(to);
    }
#else
#pragma unused(force)
#endif
    return (0 == rename(from, to));
}

static pngloss_error write_image(png24_image *output_image24, unsigned char *row_filters, const char *outname, struct pngloss_options *options)
{
    FILE *outfile;
    char *tempname = NULL;

    if (options->using_stdout) {
        set_binary_mode(stdout);
        outfile = stdout;

        if (options->verbose) {
            fprintf(stderr, "  writing compressed image to stdout\n");
        }
    } else {
        tempname = temp_filename(outname);
        if (!tempname) return OUT_OF_MEMORY_ERROR;

        if ((outfile = fopen(tempname, "wb")) == NULL) {
            fprintf(stderr, "  error: cannot open '%s' for writing\n", tempname);
            free(tempname);
            return CANT_WRITE_ERROR;
        }

        if (options->verbose) {
            fprintf(stderr, "  writing compressed image as %s\n", filename_part(outname));
        }
    }

    pngloss_error retval;
    #pragma omp critical (libpng)
    {
        retval = rwpng_write_image24(outfile, output_image24, row_filters);
    }

    if (!options->using_stdout) {
        fclose(outfile);

        if (SUCCESS == retval) {
            // Image has been written to a temporary file and then moved over destination.
            // This makes replacement atomic and avoids damaging destination file on write error.
            if (!replace_file(tempname, outname, options->force)) {
                retval = CANT_WRITE_ERROR;
            }
        }

        if (retval) {
            unlink(tempname);
        }
    }
    free(tempname);

    if (retval && retval != TOO_LARGE_FILE) {
        fprintf(stderr, "  error: failed writing image to %s (%d)\n", options->using_stdout ? "stdout" : outname, retval);
    }

    return retval;
}

static pngloss_error read_image(const char *filename, bool using_stdin, png24_image *input_image_p, bool strip, bool verbose)
{
    FILE *infile;

    if (using_stdin) {
        set_binary_mode(stdin);
        infile = stdin;
    } else if ((infile = fopen(filename, "rb")) == NULL) {
        fprintf(stderr, "  error: cannot open %s for reading\n", filename);
        return READ_ERROR;
    }

    pngloss_error retval;
    #pragma omp critical (libpng)
    {
        retval = rwpng_read_image24(infile, input_image_p, strip, verbose);
    }

    if (!using_stdin) {
        fclose(infile);
    }

    if (retval) {
        fprintf(stderr, "  error: cannot decode image %s\n", using_stdin ? "from stdin" : filename_part(filename));
        return retval;
    }

    return SUCCESS;
}

static pngloss_error prepare_output_image(png24_image *input_image, rwpng_color_transform output_color, png24_image *output_image)
{
    output_image->width = input_image->width;
    output_image->height = input_image->height;
    output_image->gamma = input_image->gamma;
    output_image->output_color = output_color;

    /*
    ** Step 3.7 [GRR]: allocate memory for the entire indexed image
    */

    output_image->rgba_data = malloc((size_t)output_image->height * (size_t)output_image->width * 4);
    output_image->row_pointers = malloc((size_t)output_image->height * sizeof(output_image->row_pointers[0]));

    if (!output_image->rgba_data || !output_image->row_pointers) {
        return OUT_OF_MEMORY_ERROR;
    }

    for(size_t row = 0; row < output_image->height; row++) {
        output_image->row_pointers[row] = output_image->rgba_data + row * output_image->width * 4;
        memcpy(output_image->row_pointers[row], input_image->row_pointers[row], output_image->width * 4);
    }

    return SUCCESS;
}
