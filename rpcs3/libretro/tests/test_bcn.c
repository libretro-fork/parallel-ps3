/* The BC1, BC2 and BC3 blocks RSX textures are decoded from on the CPU,
 * for a GPU without BCn (TextureUtils.cpp): libretro-common's
 * rdds_decode_block_bc*_bgra, which replaced RPCS3's copy of bcdec,
 * against bcdec at the commit that copy was based on (which writes RGBA;
 * RPCS3's copy wrote BGRA, red and blue swapped).
 *
 * BC1 and BC2: every texel the same, over many random blocks.
 * BC3: the colour the same. The alpha ramp is rounded now where bcdec
 * truncated it, so it is checked over every pair of end points and every
 * index against the exactly rounded value (n*a0 + (7-n)*a1 over 7, or
 * over 5), and against bcdec: the same, or one more.
 * The row pitch TextureUtils passes (a texture row, not a block row): the
 * rows land there and nothing between them is written.
 *
 * Usage: test_bcn */

#include <stdio.h>
#include <string.h>

#include <formats/rdds.h>

void bcdec_bc1(const void *compressedBlock, void *decompressedBlock, int destinationPitch);
void bcdec_bc2(const void *compressedBlock, void *decompressedBlock, int destinationPitch);
void bcdec_bc3(const void *compressedBlock, void *decompressedBlock, int destinationPitch);

typedef void (*block_fn)(const void *block, void *out, int pitch);

static unsigned long rnd_state = 0x2545F491UL;
static unsigned char rnd8(void)
{
   rnd_state = (rnd_state * 1103515245UL + 12345UL) & 0xFFFFFFFFUL;
   return (unsigned char)(rnd_state >> 16);
}

/* RGBA texels to BGRA */
static void swap_rb(unsigned char *t)
{
   int i;
   for (i = 0; i < 16; i++)
   {
      unsigned char r = t[i * 4];
      t[i * 4]     = t[i * 4 + 2];
      t[i * 4 + 2] = r;
   }
}

/* Random blocks: the BGRA decoder against bcdec's RGBA, swapped. With
 * colour_only, alpha (byte 3) is not compared */
static int same_as_bcdec(const char *name, block_fn ours, block_fn ref,
      int block_size, int colour_only)
{
   int n, i, bad = 0;
   for (n = 0; n < 200000; n++)
   {
      unsigned char blk[16], a[64], b[64];
      for (i = 0; i < block_size; i++)
         blk[i] = rnd8();
      /* every fourth BC1 block in the three-colour mode (c0 <= c1) */
      if (block_size == 8 && (n & 3) == 0 && (blk[1] > blk[3]
               || (blk[1] == blk[3] && blk[0] > blk[2])))
      {
         unsigned char t0 = blk[0], t1 = blk[1];
         blk[0] = blk[2]; blk[1] = blk[3]; blk[2] = t0; blk[3] = t1;
      }
      ours(blk, a, 16);
      ref(blk, b, 16);
      swap_rb(b);
      for (i = 0; i < 64; i++)
         if ((!colour_only || (i & 3) != 3) && a[i] != b[i])
         {
            bad++;
            break;
         }
   }
   printf("  %s: %s, 200000 random blocks, as bcdec (%d differ)\n",
         bad ? "FAIL" : "ok", name, bad);
   return bad == 0;
}

/* Every a0, a1 and index: row 0 holds indices 0-3, row 1 indices 4-7 */
static int bc3_alpha(void)
{
   int a0, a1, n, wrong = 0, off = 0, higher = 0;
   for (a0 = 0; a0 < 256; a0++)
      for (a1 = 0; a1 < 256; a1++)
      {
         /* indices 0..7 in pixels 0..7, 3 bits each, LSB first */
         unsigned long idx = 0;
         unsigned char blk[16], a[64], b[64];
         for (n = 7; n >= 0; n--)
            idx = (idx << 3) | (unsigned long)n;
         memset(blk, 0, sizeof(blk));
         blk[0] = (unsigned char)a0;
         blk[1] = (unsigned char)a1;
         blk[2] = (unsigned char)(idx & 0xFF);
         blk[3] = (unsigned char)((idx >> 8) & 0xFF);
         blk[4] = (unsigned char)((idx >> 16) & 0xFF);
         rdds_decode_block_bc3_bgra(blk, a, 16);
         bcdec_bc3(blk, b, 16);
         for (n = 0; n < 8; n++)
         {
            int want, got = a[n * 4 + 3], old = b[n * 4 + 3];
            if (n == 0)
               want = a0;
            else if (n == 1)
               want = a1;
            else if (a0 > a1) /* index 2..7: (8-n)/7 a0 + (n-1)/7 a1 */
               want = ((8 - n) * a0 + (n - 1) * a1 + 3) / 7;
            else if (n < 6)   /* 2..5: (6-n)/5 a0 + (n-1)/5 a1 */
               want = ((6 - n) * a0 + (n - 1) * a1 + 2) / 5;
            else
               want = (n == 6) ? 0 : 255;
            if (got != want)
               wrong++;
            if (got != old)
            {
               if (got == old + 1)
                  higher++;
               else
                  off++;
            }
         }
      }
   printf("  %s: BC3 alpha, every end point pair and index, rounded (%d wrong)\n",
         wrong ? "FAIL" : "ok", wrong);
   printf("  %s: BC3 alpha against bcdec's truncated ramp: %d one higher, %d otherwise different\n",
         off ? "FAIL" : "ok", higher, off);
   return !wrong && !off && higher > 0;
}

/* A 4x4 block into a wider surface, as TextureUtils writes it: the
 * packed block's rows, 64 bytes apart, and nothing else */
static int pitch(void)
{
   unsigned char surf[4 * 64], packed[64], blk[8];
   int i, row, good = 1;
   for (i = 0; i < 8; i++)
      blk[i] = rnd8();
   rdds_decode_block_bc1_bgra(blk, packed, 16);
   memset(surf, 0x5A, sizeof(surf));
   rdds_decode_block_bc1_bgra(blk, surf, 64);
   for (row = 0; row < 4; row++)
   {
      if (memcmp(surf + row * 64, packed + row * 16, 16))
         good = 0;
      for (i = 16; i < 64; i++)
         if (surf[row * 64 + i] != 0x5A)
            good = 0;
   }
   printf("  %s: a row pitch of 64 bytes: the rows there, the rest left alone\n",
         good ? "ok" : "FAIL");
   return good;
}

int main(void)
{
   int ok = 1;
   printf("test_bcn\n");
   ok &= same_as_bcdec("BC1", rdds_decode_block_bc1_bgra, bcdec_bc1, 8, 0);
   ok &= same_as_bcdec("BC2", rdds_decode_block_bc2_bgra, bcdec_bc2, 16, 0);
   ok &= same_as_bcdec("BC3 colour", rdds_decode_block_bc3_bgra, bcdec_bc3, 16, 1);
   ok &= bc3_alpha();
   ok &= pitch();
   printf(ok ? "test_bcn: ok\n" : "test_bcn: FAILED\n");
   return ok ? 0 : 1;
}
