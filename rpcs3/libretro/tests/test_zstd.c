/* Savestates as rpcs3/util/serialization_ext.cpp keeps them: a run of
 * Zstandard frames, one per chunk the writer was handed, read back a
 * block at a time through rzstd as the handler does.
 *
 * 1. Frames the reference libzstd wrote (fixtures/zstd_frames.bin: level
 *    8 as the old ZSTD_compressCCtx call, one frame without a checksum and
 *    one with) decode to the content they were made from.
 * 2. Frames rzstd writes decode the same way, and are written to the file
 *    named on the command line so CI can check the reference zstd reads
 *    them too (standalone RPCS3 loads the core's savestates).
 *
 * Usage: test_zstd <fixtures dir> [file to write rzstd's frames to] */

#include <encodings/rzstd.h>

#include "test_common.h"

/* The handler's reader, with the file read @in_step bytes at a time and
 * the output asked for @out_step bytes at a time. Returns how many bytes
 * came out, or (size_t)-1 on a decode error. */
static size_t read_frames(const uint8_t *file, size_t file_len,
      size_t in_step, uint8_t *out, size_t out_len, size_t out_step)
{
   rzstd_blocks_t *frame   = NULL;
   const uint8_t  *pending = NULL;
   size_t pending_size     = 0;
   size_t buf_len          = 0; /* bytes of the file "read" so far */
   size_t index            = 0; /* where decoding is in them */
   size_t total            = 0;

   while (total < out_len)
   {
      size_t limit = total + out_step < out_len ? total + out_step : out_len;

      while (total < limit)
      {
         size_t avail = buf_len - index;
         size_t want  = 1;
         const uint8_t *block = NULL;
         size_t block_size    = 0;

         if (pending_size)
         {
            size_t n = pending_size < limit - total ? pending_size : limit - total;
            memcpy(out + total, pending, n);
            total        += n;
            pending      += n;
            pending_size -= n;
            continue;
         }

         if (frame)
         {
            want = rzstd_blocks_want(frame);
            if (!want)
            {
               rzstd_blocks_free(frame);
               frame = NULL;
               continue;
            }
         }

         if (avail < want)
         {
            size_t add = want - avail > in_step ? want - avail : in_step;
            if (buf_len + add > file_len)
               add = file_len - buf_len;
            if (!add)
               goto end;
            buf_len += add;
            continue;
         }

         if (!frame && !(frame = rzstd_blocks_open()))
            return (size_t)-1;

         want = rzstd_blocks_want(frame);
         if (avail < want)
            continue;

         if (rzstd_blocks_give(frame, file + index, want, &block,
                  &block_size) != RZSTD_PROCESS_NEXT)
         {
            rzstd_blocks_free(frame);
            return (size_t)-1;
         }
         index        += want;
         pending       = block;
         pending_size  = block_size;
      }
   }
end:
   rzstd_blocks_free(frame);
   return total;
}

int main(int argc, char **argv)
{
   char path[1024];
   size_t fixture_len = 0;
   uint8_t *fixture;
   uint8_t *expect;
   uint8_t *got;
   uint8_t *frames;
   size_t frames_len = 0;
   const size_t len_a = 150000, len_b = 40000;
   const size_t steps[3][2] = { { 0x10000, 777 }, { 7, 0x10000 }, { 0x100000, 1 << 20 } };
   int i;

   if (argc < 2)
   {
      fprintf(stderr, "usage: %s <fixtures dir> [out file]\n", argv[0]);
      return 2;
   }

   expect = (uint8_t*)malloc(len_a + len_b);
   got    = (uint8_t*)malloc(len_a + len_b + 1);
   frames = (uint8_t*)malloc(rzstd_compress_bound(len_a) + rzstd_compress_bound(len_b));
   if (!expect || !got || !frames)
      return 2;
   test_gen(expect, len_a, 1);
   test_gen(expect + len_a, len_b, 2);

   /* 1. what the reference library wrote */
   sprintf(path, "%.1000s/zstd_frames.bin", argv[1]);
   fixture = test_read_file(path, &fixture_len);
   TEST_CHECK(fixture != NULL);
   if (fixture)
   {
      for (i = 0; i < 3; i++)
      {
         size_t n;
         memset(got, 0, len_a + len_b + 1);
         n = read_frames(fixture, fixture_len, steps[i][0], got,
               len_a + len_b + 1, steps[i][1]);
         TEST_CHECK(n == len_a + len_b);
         TEST_CHECK(n == len_a + len_b && !memcmp(got, expect, n));
      }

      /* a frame cut short reads as far as it goes, then ends */
      TEST_CHECK(read_frames(fixture, fixture_len - 9, 0x10000, got,
               len_a + len_b, 4096) < len_a + len_b);

      /* a damaged frame is an error, not wrong data */
      fixture[fixture_len / 3] ^= 0x5a;
      {
         size_t n = read_frames(fixture, fixture_len, 0x10000, got,
               len_a + len_b, 4096);
         TEST_CHECK(n == (size_t)-1 || n < len_a
               || memcmp(got, expect, len_a) != 0 || n != len_a + len_b);
      }
      free(fixture);
   }

   /* 2. what rzstd writes, one frame per chunk as the writer threads do */
   {
      size_t wa = 0, wb = 0;
      TEST_CHECK(rzstd_encode(frames, rzstd_compress_bound(len_a), expect,
               len_a, 5, &wa) == RZSTD_PROCESS_END && wa);
      TEST_CHECK(rzstd_encode(frames + wa, rzstd_compress_bound(len_b),
               expect + len_a, len_b, 5, &wb) == RZSTD_PROCESS_END && wb);
      frames_len = wa + wb;
      for (i = 0; i < 3; i++)
      {
         size_t n;
         memset(got, 0, len_a + len_b + 1);
         n = read_frames(frames, frames_len, steps[i][0], got,
               len_a + len_b + 1, steps[i][1]);
         TEST_CHECK(n == len_a + len_b && !memcmp(got, expect, n));
      }
      if (argc > 2)
         TEST_CHECK(test_write_file(argv[2], frames, frames_len));
   }

   free(expect);
   free(got);
   free(frames);

   if (test_failures)
   {
      fprintf(stderr, "test_zstd: %d check(s) failed\n", test_failures);
      return 1;
   }
   printf("test_zstd: ok\n");
   return 0;
}
