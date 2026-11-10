/* cellPngDec's pixel conversions on rows rpng_rows decoded, done as the
 * libpng the module was written against does them (1.6), in its order:
 * expand (palette to RGB, gray under 8 bits to 8, tRNS to alpha), strip
 * alpha, RGB to gray, strip 16 bits to 8, add a filler, swap alpha. And
 * the two ways a row of an interlaced image is put into the picture:
 * only its own pixels, or the "blocky" display that also fills the
 * pixels later passes will draw (png_progressive_combine_row).
 *
 * RGB to gray weighs the channels by the file's chromaticities (mDCV,
 * else cHRM unless sRGB) and, when the file has a gamma (gAMA, sRGB),
 * mixes in linear light through libpng's tables, built once at setup;
 * a file with neither takes the plain path it always took. C89. */

#ifndef CELLPNGDEC_ROWS_H
#define CELLPNGDEC_ROWS_H

#include <stddef.h>
#include <stdint.h>

#include <formats/rpng_rows.h>

#ifdef __cplusplus
extern "C" {
#endif

enum
{
   PDR_EXPAND        = 1 << 0, /* png_set_palette_to_rgb, png_set_expand_gray_1_2_4_to_8 */
   PDR_EXPAND_TRNS   = 1 << 1, /* png_set_palette_to_rgb, png_set_tRNS_to_alpha */
   PDR_STRIP_ALPHA   = 1 << 2, /* png_set_strip_alpha */
   PDR_RGB_TO_GRAY   = 1 << 3, /* png_set_rgb_to_gray, default coefficients
                                  (so the file's chromaticities, if any) */
   PDR_STRIP_16      = 1 << 4, /* png_set_strip_16 */
   PDR_ADD_ALPHA     = 1 << 5, /* png_set_add_alpha */
   PDR_FILLER_BEFORE = 1 << 6, /* ... with PNG_FILLER_BEFORE */
   PDR_SWAP_ALPHA    = 1 << 7  /* png_set_swap_alpha */
};

/* cellPngDec's colour spaces and alpha selection, as CellPngDecColorSpace
 * and CellPngDecOutputAlphaSelect number them */
enum
{
   PDR_CS_GRAYSCALE       = 1,
   PDR_CS_RGB             = 2,
   PDR_CS_PALETTE         = 4,
   PDR_CS_GRAYSCALE_ALPHA = 9,
   PDR_CS_RGBA            = 10,
   PDR_CS_ARGB            = 20,
   PDR_STREAM_ALPHA       = 0,
   PDR_FIX_ALPHA          = 1
};

/* The colour space cellPngDec reports for a PNG colour type; -1 if none */
int pdr_color_space(unsigned color_type);

/* The conversions cellPngDecSetParameter asks for, as it asked libpng:
 * @out_space, @alpha_select and @alpha are the game's output colour
 * space, alpha selection and fixed alpha, @out_depth its bit depth.
 * Writes the filler pdr_setup takes. Sets @unsupported when the game
 * asks for a conversion the module never did (gray or palette to gray
 * or palette). */
unsigned pdr_flags_for(const struct rpng_rows_info *info, int out_space,
      int alpha_select, unsigned alpha, unsigned out_depth, unsigned *filler,
      int *unsupported);

struct pdr_format
{
   uint8_t color_type; /* PNG colour type */
   uint8_t depth;      /* bits a sample */
   uint8_t channels;
};

struct pdr
{
   const struct rpng_rows_info *info;
   unsigned          flags;
   uint16_t          filler;
   struct pdr_format out;        /* what the conversions make */
   size_t            out_rowbytes;/* of a whole row, as libpng reports it */
   unsigned          pixel_bits; /* of each pixel written */
   int               valid;      /* 0: libpng would have failed on this set */
   uint8_t          *work;       /* two rows at 8 bytes a pixel, and one more */
   size_t            work_cap;

   /* RGB to gray, as png_init_read_transformations leaves it */
   uint32_t          gray_red;   /* coefficients, /32768 */
   uint32_t          gray_green;
   int               gray_gamma; /* through the gamma tables */
   unsigned          gamma_shift;/* 16 bits: low bits the tables drop */
   uint8_t           gamma_8[3][256];  /* 8 bits: file to screen, to linear,
                                          from linear */
   uint16_t         *gamma_16;   /* 16 bits: the same, each 256 entries
                                    for each of the 256 >> shift low bits */
   size_t            gamma_16_cap;
};

/* Settles the output format for @flags, as png_read_update_info, and
 * takes the work rows (and gamma tables, for RGB to gray on a 16-bit file
 * with a gamma). Called once the header is read; the memory stays for the
 * next image that fits. Returns 0 when out of memory. */
int pdr_setup(struct pdr *p, const struct rpng_rows_info *info,
      unsigned flags, unsigned filler);

void pdr_free(struct pdr *p);

/* The row's pixels converted, @width of them, in p->out's format, in
 * the work memory until the next call; @row itself, lent back, when no
 * conversion applies */
const uint8_t *pdr_convert(struct pdr *p, const uint8_t *row, uint32_t width);

/* A converted pass row stretched to the whole width, each pixel across
 * the columns its pass steps over, as png_do_read_interlace leaves it:
 * what libpng hands a progressive reader. @pass 0..6. */
const uint8_t *pdr_expand_pass(struct pdr *p, const uint8_t *row,
      uint32_t width, unsigned pass);

/* png_combine_row: a whole-width row @src into @dst, every pixel for a
 * plain image or pass 7, else the pass's own pixels (@display 0) or
 * those and the ones later passes draw (@display 1). @pass 0..6, or -1
 * for a plain image. */
void pdr_combine(const struct pdr *p, uint8_t *dst, const uint8_t *src,
      int pass, int display);

/* A pass row's own pixels into their columns of @dst, for a reader that
 * reads row by row (png_read_row with interlace handling). */
void pdr_scatter(const struct pdr *p, uint8_t *dst, const uint8_t *src,
      const struct rpng_rows_row *row);

/* The rows libpng's progressive reader hands its row callback, in its
 * order: for an interlaced image every image row of every pass, a pass
 * row repeated over the rows it stands for until later passes draw them,
 * and a null row where a pass has none (png_push_process_row). */
typedef void (*pdr_row_fn)(void *userdata, const uint8_t *row,
      uint32_t row_num, unsigned pass);

struct pdr_push
{
   uint32_t row_number;
   unsigned pass;
};

void pdr_push_begin(struct pdr_push *s);

/* A decoded row, converted: calls @fn as libpng's reader would */
void pdr_push_row(struct pdr *p, struct pdr_push *s, const uint8_t *row,
      uint32_t width, pdr_row_fn fn, void *userdata);

#ifdef __cplusplus
}
#endif

#endif
