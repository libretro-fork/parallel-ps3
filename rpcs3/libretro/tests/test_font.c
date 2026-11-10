/* The stb_truetype replacement: libretro-common's rtt, as the core uses
 * it, against stb_truetype (the stb RPCS3 pinned) on the same fonts:
 *
 * - the overlays' atlas: whole 256-code-point pages packed by the core's
 *   packer (Emu/RSX/Overlays/overlay_font_pack.h) and by stb's, with the
 *   overlays' settings, at the sizes they use: each glyph's cell, offsets
 *   and advance must be the same, and the drawn texels close;
 * - cellFont: each glyph drawn on its own at the game's height, as
 *   cellFontRenderCharGlyphImage draws it, and the metrics
 *   cellFontGetHorizontalLayout and cellFontGetCharGlyphMetrics give.
 *
 * Fonts: DejaVu Sans (TrueType outlines; Bitstream Vera licence) cut to
 * Latin-1 and Cyrillic, and Noto Sans CJK JP (CID-keyed CFF outlines; SIL
 * Open Font License) cut to the page U+4E00, as an overlay's CJK pages
 * use. Both are free to redistribute, cut down too.
 *
 * The texels are not stb's: both rasterizers cover a pixel by the exact
 * area of the outline in it, but draw curves as straight pieces, and the
 * core has rtt draw finer pieces (RPCS3_FONT_FLATNESS) than stb did. Each
 * is measured against the true coverage - rtt's own with the curves cut
 * into pieces 0.005 pixels from the curve, which agrees with coverage
 * computed exactly from the outline to within a unit - and the core's
 * must be at least as close as stb's was, on average and in its worst
 * texel.
 *
 * Usage: test_font fixtures-dir */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#include <encodings/deflate.h>
#include <gfx/font/rtt.h>

#include "stb_truetype.h"
#include "../../Emu/RSX/Overlays/overlay_font_pack.h"

static int test_failures = 0;

#define TEST_CHECK(cond) do { if (!(cond)) { \
   fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
   test_failures++; } } while (0)

/* The overlays' atlas (overlay_fonts.h) */
#define ATLAS_W 1024
#define ATLAS_H 1024
#define OVERSAMPLE 2

/* How far the atlas may be from stb's: its texels differ from the true
 * coverage by up to about 60 on curves, as it cuts them coarser */
#define MAX_STB_TEXEL 80
#define MAX_STB_MEAN  0.6

/* The true coverage, near enough */
#define TRUE_FLATNESS 0.005f

/* How far @a is from the true coverage @t, over @n texels */
struct err
{
   double sum;
   int    worst;
};

static void add_err(struct err *e, const uint8_t *a, const uint8_t *t,
      size_t n)
{
   size_t i;
   for (i = 0; i < n; i++)
   {
      int d = abs((int)a[i] - (int)t[i]);
      if (d > e->worst)
         e->worst = d;
      e->sum += d;
   }
}

static uint8_t *gunzip(const char *path, size_t *out_len)
{
   size_t   in_len = 0, cap = 1 << 20, taken = 0, wrote = 0;
   uint8_t *in, *out;
   void    *z;
   int      r;
   FILE    *f = fopen(path, "rb");
   if (!f)
      return NULL;
   fseek(f, 0, SEEK_END);
   in_len = (size_t)ftell(f);
   fseek(f, 0, SEEK_SET);
   in = (uint8_t*)malloc(in_len);
   if (fread(in, 1, in_len, f) != in_len)
      in_len = 0;
   fclose(f);
   out = (uint8_t*)malloc(cap);
   z   = rinflate_new(16 + 15);
   rinflate_set_in(z, in, in_len);
   rinflate_set_out(z, out, cap);
   r = rinflate_process(z, &taken, &wrote);
   rinflate_free(z);
   free(in);
   if (r != RDEFLATE_PROCESS_END)
   {
      free(out);
      return NULL;
   }
   *out_len = wrote;
   return out;
}

static int same_cell(const stbtt_packedchar *s, const font_packed_char *r)
{
   return s->x0 == r->x0 && s->y0 == r->y0 && s->x1 == r->x1
       && s->y1 == r->y1 && s->xoff == r->xoff && s->yoff == r->yoff
       && s->xoff2 == r->xoff2 && s->yoff2 == r->yoff2
       && s->xadvance == r->xadvance;
}

/* One page of the atlas, both ways */
static void check_page(const char *name, const uint8_t *ttf, size_t len,
      rtt_font_t *rf, rtt_scratch_t *sc, uint32_t page, float px)
{
   static uint8_t   s_pix[ATLAS_W * ATLAS_H], r_pix[ATLAS_W * ATLAS_H];
   static uint8_t   t_pix[ATLAS_W * ATLAS_H];
   stbtt_packedchar s_pack[256];
   font_packed_char r_pack[256], t_pack[256];
   stbtt_pack_context ctx;
   struct err s_err = { 0, 0 }, r_err = { 0, 0 }, d_err = { 0, 0 };
   int    i, s_ok, r_ok, cells = 0;

   memset(s_pack, 0, sizeof(s_pack));
   memset(r_pack, 0, sizeof(r_pack));
   (void)len;

   /* stb as overlay_fonts.cpp called it */
   s_ok = stbtt_PackBegin(&ctx, s_pix, ATLAS_W, ATLAS_H, 0, 1, NULL);
   stbtt_PackSetOversampling(&ctx, OVERSAMPLE, OVERSAMPLE);
   s_ok = s_ok && stbtt_PackFontRange(&ctx, ttf, 0, px, (int)(page * 256), 256, s_pack);
   stbtt_PackEnd(&ctx);

   rtt_set_flatness(rf, RPCS3_FONT_FLATNESS);
   r_ok = font_pack_range(rf, sc, r_pix, ATLAS_W, ATLAS_H, px, page * 256,
         256, OVERSAMPLE, r_pack);
   rtt_set_flatness(rf, TRUE_FLATNESS);
   font_pack_range(rf, sc, t_pix, ATLAS_W, ATLAS_H, px, page * 256,
         256, OVERSAMPLE, t_pack);

   TEST_CHECK(s_ok == r_ok);
   for (i = 0; i < 256; i++)
   {
      if (same_cell(&s_pack[i], &r_pack[i]))
         cells++;
      else if (cells == i)
         fprintf(stderr, "%s page %02X at %g px: U+%04X's cell differs: stb %d,%d-%d,%d %g,%g adv %g; rtt %d,%d-%d,%d %g,%g adv %g\n",
               name, page, px, page * 256 + i,
               s_pack[i].x0, s_pack[i].y0, s_pack[i].x1, s_pack[i].y1, s_pack[i].xoff, s_pack[i].yoff, s_pack[i].xadvance,
               r_pack[i].x0, r_pack[i].y0, r_pack[i].x1, r_pack[i].y1, r_pack[i].xoff, r_pack[i].yoff, r_pack[i].xadvance);
   }
   TEST_CHECK(cells == 256);

   /* and placed as stb placed them, pen and all */
   {
      float sx = 3.5f, sy = 7.25f, rx = 3.5f, ry = 7.25f;
      int   quads = 0;
      for (i = 0; i < 256; i++)
      {
         stbtt_aligned_quad sq;
         font_quad          rq;
         stbtt_GetPackedQuad(s_pack, ATLAS_W, ATLAS_H, i, &sx, &sy, &sq, 0);
         font_packed_quad(&r_pack[i], ATLAS_W, ATLAS_H, &rx, &ry, &rq);
         if (sq.x0 == rq.x0 && sq.y0 == rq.y0 && sq.x1 == rq.x1 && sq.y1 == rq.y1
               && sq.s0 == rq.s0 && sq.t0 == rq.t0 && sq.s1 == rq.s1 && sq.t1 == rq.t1
               && sx == rx && sy == ry)
            quads++;
      }
      TEST_CHECK(quads == 256);
   }

   add_err(&s_err, s_pix, t_pix, sizeof(t_pix));
   add_err(&r_err, r_pix, t_pix, sizeof(t_pix));
   add_err(&d_err, r_pix, s_pix, sizeof(t_pix));
   printf("test_font: %s page %02X at %2g px: %d cells the same; off the true coverage: stb %.4f on average, %3d at worst, rtt %.4f, %3d\n",
         name, page, px, cells, s_err.sum / sizeof(t_pix), s_err.worst,
         r_err.sum / sizeof(t_pix), r_err.worst);
   TEST_CHECK(r_err.sum <= s_err.sum);
   TEST_CHECK(r_err.worst <= s_err.worst);
   /* and drawn as stb drew them, filters and all, but for the finer curves */
   TEST_CHECK(d_err.worst <= MAX_STB_TEXEL);
   TEST_CHECK(d_err.sum / sizeof(t_pix) <= MAX_STB_MEAN);
}

/* cellFont: metrics, and each glyph drawn alone, at @height */
static void check_cellfont(const char *name, const stbtt_fontinfo *si,
      rtt_font_t *rf, rtt_scratch_t *sc, uint32_t first,
      uint32_t count, float height)
{
   static uint8_t r_box[512 * 512], t_box[512 * 512];
   struct err s_err = { 0, 0 }, r_err = { 0, 0 };
   int      s_asc, s_desc, s_gap, r_asc, r_desc, r_gap;
   float    s_scale = stbtt_ScaleForPixelHeight(si, height);
   float    r_scale;
   uint32_t c;
   int      boxes = 0, metrics = 0;

   stbtt_GetFontVMetrics(si, &s_asc, &s_desc, &s_gap);
   rtt_vmetrics(rf, &r_asc, &r_desc, &r_gap);
   r_scale = height / (float)(r_asc - r_desc);
   TEST_CHECK(s_asc == r_asc && s_desc == r_desc && s_gap == r_gap);
   TEST_CHECK(s_scale == r_scale);

   for (c = first; c < first + count; c++)
   {
      int s_w = 0, s_h = 0, s_xo = 0, s_yo = 0;
      int s_x0 = 0, s_y0 = 0, s_x1 = 0, s_y1 = 0, s_adv = 0, s_lsb = 0;
      int r_x0 = 0, r_y0 = 0, r_x1 = 0, r_y1 = 0, r_adv = 0, r_lsb = 0;
      int gi = rtt_find_glyph(rf, c);
      int has;
      unsigned char *s_box;

      TEST_CHECK(gi == stbtt_FindGlyphIndex(si, (int)c));

      /* cellFontGetCharGlyphMetrics */
      stbtt_GetCodepointBox(si, (int)c, &s_x0, &s_y0, &s_x1, &s_y1);
      stbtt_GetCodepointHMetrics(si, (int)c, &s_adv, &s_lsb);
      has = rtt_glyph_box(rf, gi, &r_x0, &r_y0, &r_x1, &r_y1);
      rtt_glyph_hmetrics(rf, gi, &r_adv, &r_lsb);
      if (s_x0 == r_x0 && s_y0 == r_y0 && s_x1 == r_x1 && s_y1 == r_y1
            && s_adv == r_adv && s_lsb == r_lsb)
         metrics++;

      /* cellFontRenderCharGlyphImage */
      s_box = stbtt_GetCodepointBitmap(si, s_scale, s_scale, (int)c, &s_w, &s_h, &s_xo, &s_yo);
      if (!has || !s_box)
      {
         if (!has && !s_box)
            boxes++;
         stbtt_FreeBitmap(s_box, NULL);
         continue;
      }
      if (s_xo == (int)floor((float)r_x0 * r_scale)
            && s_yo == (int)floor((float)-r_y1 * r_scale)
            && s_w == (int)ceil((float)r_x1 * r_scale) - s_xo
            && s_h == (int)ceil((float)-r_y0 * r_scale) - s_yo
            && s_w <= 512 && s_h <= 512)
      {
         boxes++;
         rtt_set_flatness(rf, RPCS3_FONT_FLATNESS);
         rtt_render_glyph(rf, sc, r_box, s_w, s_h, s_w, r_scale, r_scale,
               gi, r_x0, r_y1, 0);
         rtt_set_flatness(rf, TRUE_FLATNESS);
         rtt_render_glyph(rf, sc, t_box, s_w, s_h, s_w, r_scale, r_scale,
               gi, r_x0, r_y1, 0);
         add_err(&s_err, s_box, t_box, (size_t)(s_w * s_h));
         add_err(&r_err, r_box, t_box, (size_t)(s_w * s_h));
      }
      stbtt_FreeBitmap(s_box, NULL);
   }
   printf("test_font: %s cellFont at %2g px: metrics the same for %d of %u glyphs, boxes for %d; off the true coverage: stb %3d at worst, rtt %3d\n",
         name, height, metrics, (unsigned)count, boxes, s_err.worst, r_err.worst);
   TEST_CHECK(metrics == (int)count);
   TEST_CHECK(boxes == (int)count);
   TEST_CHECK(r_err.sum <= s_err.sum);
   TEST_CHECK(r_err.worst <= s_err.worst);
}

int main(int argc, char **argv)
{
   static const char *const files[] = { "dejavu_sans_sub.ttf.gz", "noto_cjk_sub.otf.gz" };
   static const uint32_t pages[][3]  = { { 2, 0x00, 0x04 }, { 1, 0x4E, 0 } };
   static const float    sizes[]     = { 16.f, 32.f, 64.f };
   char path[1024];
   int  f;

   if (argc < 2)
      return 2;

   for (f = 0; f < 2; f++)
   {
      size_t         len = 0;
      uint8_t       *ttf;
      stbtt_fontinfo si;
      rtt_font_t     rf;
      rtt_scratch_t  sc;
      unsigned       p, s;

      memset(&sc, 0, sizeof(sc));
      sprintf(path, "%s/%s", argv[1], files[f]);
      ttf = gunzip(path, &len);
      TEST_CHECK(ttf != NULL);
      if (!ttf)
         continue;

      TEST_CHECK(stbtt_GetFontOffsetForIndex(ttf, 0) == rtt_font_offset_for_index(ttf, len, 0));
      TEST_CHECK(stbtt_InitFont(&si, ttf, 0) != 0);
      TEST_CHECK(rtt_init(&rf, ttf, len, 0) != 0);

      for (p = 1; p <= pages[f][0]; p++)
      {
         for (s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++)
         {
            check_page(files[f], ttf, len, &rf, &sc, pages[f][p], sizes[s]);
            check_cellfont(files[f], &si, &rf, &sc, pages[f][p] * 256, 256, sizes[s]);
         }
      }

      rtt_scratch_free(&sc);
      free(ttf);
   }

   if (test_failures)
   {
      fprintf(stderr, "test_font: %d check(s) failed\n", test_failures);
      return 1;
   }
   printf("test_font: ok\n");
   return 0;
}
