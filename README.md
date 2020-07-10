pngloss
=======

Lossily compress your PNG images with pngloss. The program reads the original
PNG file, modifies the pixels to make them more compressible, and writes a new
file with the extension -loss.png.

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

`pngloss [options] <quality> <file> [<file>...]`

Quality ranges from 1 to 100. Quality 100 is lossless and does not modify the
pixel data, although it may convert colorspace or strip PNG chunks.

### Options

`-v`, `--verbose`
Verbose - print additional information about compression

`-q`, `--quiet`
Quiet - don't print information about compression

`-f`, `--force`
Force - overwrite existing output image

`--no-force`
Don't overwrite existing output image - overrides an earlier "force" argument

`--ext`
Specify filename extension. Defaults to "-loss.png"

`--skip-if-larger`
Don't write compressed image if it's larger than the original

`-o`, `--output`
Output filename. When this option is given only one input file is accepted

`--strip`
Remove unnecessary chunks (metadata) from input file when writing output

`-V`, `--version`
Print version number

`-h`, `--help`
Display usage information
