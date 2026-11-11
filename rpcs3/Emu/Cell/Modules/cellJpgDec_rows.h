/* cellJpgDec's decode: a JPEG decoded by rjpeg_decode_rows a row at a
 * time, each row put straight into the game's buffer in the colour space
 * it asked for - RGBA, RGB or ARGB, top to bottom or bottom to top - at
 * its line pitch, as the module laid out the whole picture before. An
 * RGBA row that fits is decoded where it goes; the others are converted
 * from the decoder's row into place. Nothing is allocated here: the
 * decoder keeps its memory from one image to the next. C89. */

#ifndef CELLJPGDEC_ROWS_H
#define CELLJPGDEC_ROWS_H

#include <stddef.h>
#include <stdint.h>

#include <boolean.h>
#include <formats/rjpeg.h>

#ifdef __cplusplus
extern "C" {
#endif

/* cellJpgDec's colour spaces, as CellJpgDecColorSpace numbers them; any
 * other is decoded and nothing written, as the module always did */
enum
{
   JDR_CS_RGB  = 2,
   JDR_CS_RGBA = 10,
   JDR_CS_ARGB = 20
};

/**
 * jdr_decode:
 * @dec            : the decoder
 * @jpg, @len      : the file, read where it lies
 * @out            : the game's buffer
 * @bytes_per_line : the game's line pitch (outputBytesPerLine)
 * @bottom_to_top  : CELL_JPGDEC_BOTTOM_TO_TOP
 * @space          : the output colour space
 * @width, @height : receive the image's size
 *
 * Rows of @bytes_per_line bytes (each cut to it) where that is wider
 * than a row of the image or the picture is upside down, packed rows
 * otherwise. A line cut inside a pixel takes that pixel's first bytes.
 *
 * Returns: false if the file is not a JPEG or is broken; the rows
 * decoded before the break are in place.
 */
bool jdr_decode(rjpeg_t *dec, const uint8_t *jpg, size_t len,
      uint8_t *out, size_t bytes_per_line, bool bottom_to_top, int space,
      unsigned *width, unsigned *height);

/* Bytes a pixel of @space, 0 for one written as nothing */
unsigned jdr_components(int space);

#ifdef __cplusplus
}
#endif

#endif
