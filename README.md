pngloss
=======

Lossily compress your PNG images with pngloss. The program reads the original
PNG file, modifies the pixels to make them more compressible, and writes a new
file with the extension -loss.png.

The compression technique relies on making small adjustments to pixel colors.
It works best on true-color images with a wide variety of colors, like
photographs or computer generated graphics with realistic lighting. It does
not do a good job on paletted images or images with large areas of flat color.

### Heritage

The lossy compression in pngloss is based on an algorithm in Michael Vinther's
graphics editor, [Image Analyzer](http://meesoft.logicnet.dk/Analyzer/).

The command line tool is based on Kornel Lesiński's PNG quantization tool,
[pngquant](https://pngquant.org/). Additionally, pngloss includes his work to
port the lossy compression algorithm from Go to C as part of his PNG
compression suite, [ImageOptim](https://imageoptim.com/).

William MacKay brought these pieces together and did original research to
improve the lossy compression algorithm.

### Synopsis

`pngloss [options] <file> [<file>...]`

### Options

`-s`, `--strength`
How much quality to sacrifice, from 0 to 255 (default 26). Strength 0 is
lossless and does not modify the pixel data, although it may convert
colorspace or strip PNG chunks.

`-b`, `--bleed`
Color bleed divider, from 1 to 32767 (default 2). A divider of 1
propagates all of the error from quantization to neighboring pixels, which
improves visual quality but also increases filesize. The default of 2
propagates half (1/2) of the error, which is usually a good tradeoff.

`-v`, `--verbose`
Verbose - print additional information about compression.

`-q`, `--quiet`
Quiet - don't print information about compression (the default).

`-f`, `--force`
Force - overwrite existing output image.

`--no-force`
Don't overwrite existing output image - overrides an earlier "force" argument.

`--ext`
Specify filename extension. Defaults to "-loss.png".

`--skip-if-larger`
Don't write compressed image if it's larger than the original.

`-o`, `--output`
Output filename. When this option is given only one input file is accepted.

`--strip`
Remove unnecessary chunks (metadata) from input file when writing output.

`-V`, `--version`
Print version number.

`-h`, `--help`
Display usage information.
