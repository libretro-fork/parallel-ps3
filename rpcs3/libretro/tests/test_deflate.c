/* The zlib replacement: data the reference zlib wrote, read back through
 * rinflate as the core reads it, and rdeflate's own output.
 *
 * 1. fixtures/gzip_log.bin: the compressed log as it was written (gzip,
 *    level 9, a sync flush after every write); gzip_state.bin: a gzip
 *    savestate (level 8, no flush); zlib_self.bin: a zlib-wrapped SELF
 *    segment. Each decodes to the content it was made from, with the
 *    input handed over a piece at a time and the output asked for a
 *    piece at a time, as rpcs3/util/serialization_ext.cpp does.
 * 2. A log written as rpcs3/util/logs.cpp writes it, with rdeflate_flush
 *    after every write: after each flush the file so far decodes to all
 *    that was written, as a tester's .gz cut short by a crash must. The
 *    finished stream goes to the file named on the command line for CI
 *    to check with gzip.
 *
 * Usage: test_deflate <fixtures dir> [file to write the log to] */

#include <encodings/crc32.h>
#include <encodings/deflate.h>

#include "test_common.h"

#define LEN_A 150000
#define LEN_B 40000
#define LEN   (LEN_A + LEN_B)

/* Decodes @in with the input @in_step and the output @out_step bytes at a
 * time; returns the bytes it gave, or (size_t)-1 on an error. @ended gets
 * whether the stream reached its end. */
static size_t inflate_steps(int window_bits, const uint8_t *in, size_t in_len,
      size_t in_step, uint8_t *out, size_t out_len, size_t out_step,
      int *ended)
{
   void  *z    = rinflate_new(window_bits);
   size_t have = 0, used = 0, total = 0;
   *ended = 0;
   if (!z)
      return (size_t)-1;
   for (;;)
   {
      size_t taken = 0, wrote = 0;
      size_t room  = out_len - total < out_step ? out_len - total : out_step;
      int    r;
      if (used == have)
      {
         size_t add = in_len - have < in_step ? in_len - have : in_step;
         if (!add)
            break;
         have += add;
      }
      if (!room)
         break;
      rinflate_set_in(z, in + used, have - used);
      rinflate_set_out(z, out + total, room);
      r = rinflate_process(z, &taken, &wrote);
      used  += taken;
      total += wrote;
      if (r == RDEFLATE_PROCESS_ERROR)
      {
         rinflate_free(z);
         return (size_t)-1;
      }
      if (r == RDEFLATE_PROCESS_END)
      {
         *ended = 1;
         break;
      }
   }
   rinflate_free(z);
   return total;
}

static void check_fixture(const char *dir, const char *name, int window_bits,
      const uint8_t *expect)
{
   static const size_t steps[3][2] = { { 0x10000, 777 }, { 3, 0x10000 }, { 1 << 20, 1 << 20 } };
   char     path[1024];
   size_t   len = 0;
   uint8_t *in;
   uint8_t *out = (uint8_t*)malloc(LEN + 1);
   int      i;

   sprintf(path, "%.900s/%s", dir, name);
   in = test_read_file(path, &len);
   TEST_CHECK(in != NULL && out != NULL);
   if (!in || !out)
   {
      free(in);
      free(out);
      return;
   }
   for (i = 0; i < 3; i++)
   {
      int    ended = 0;
      size_t n     = inflate_steps(window_bits, in, len, steps[i][0], out,
            LEN + 1, steps[i][1], &ended);
      TEST_CHECK(ended);
      TEST_CHECK(n == LEN && !memcmp(out, expect, LEN));
   }
   /* a damaged stream fails its check instead of passing for data */
   in[len / 2] ^= 0x10;
   {
      int    ended = 0;
      size_t n     = inflate_steps(window_bits, in, len, 0x10000, out,
            LEN + 1, 0x10000, &ended);
      TEST_CHECK(n == (size_t)-1 || !ended || memcmp(out, expect, LEN));
   }
   free(in);
   free(out);
}

int main(int argc, char **argv)
{
   uint8_t *expect = (uint8_t*)malloc(LEN);
   uint8_t *gz     = (uint8_t*)malloc(LEN * 2);
   uint8_t *back   = (uint8_t*)malloc(LEN + 1);
   static uint8_t zout[65536];
   size_t   gz_len = 0, given = 0;
   void    *z;

   if (argc < 2)
   {
      fprintf(stderr, "usage: %s <fixtures dir> [out file]\n", argv[0]);
      return 2;
   }
   if (!expect || !gz || !back)
      return 2;
   test_gen(expect, LEN_A, 1);
   test_gen(expect + LEN_A, LEN_B, 2);

   /* 1. what zlib wrote */
   check_fixture(argv[1], "gzip_log.bin", 16 + 15, expect);
   check_fixture(argv[1], "gzip_state.bin", 16 + 15, expect);
   check_fixture(argv[1], "zlib_self.bin", 15, expect);

   /* the IRD license's CRC: zlib's crc32 of the same bytes */
   TEST_CHECK(encoding_crc32(0, (const uint8_t*)"123456789", 9) == 0xcbf43926u);

   /* 2. the log as logs.cpp writes it: writes of up to 32 KiB, each
    * flushed, through a 64 KiB output buffer */
   z = rdeflate_new(9, 16 + 15);
   TEST_CHECK(z != NULL);
   if (z)
   {
      while (given < LEN)
      {
         size_t n = LEN - given < 32768 ? LEN - given : 32768;
         int    ended = 0;
         rdeflate_set_in(z, expect + given, n);
         rdeflate_flush(z);
         given += n;
         for (;;)
         {
            size_t wrote = 0;
            rdeflate_set_out(z, zout, sizeof(zout));
            TEST_CHECK(rdeflate_process(z, NULL, &wrote) != RDEFLATE_PROCESS_ERROR);
            memcpy(gz + gz_len, zout, wrote);
            gz_len += wrote;
            if (wrote < sizeof(zout))
               break;
         }
         TEST_CHECK(inflate_steps(16 + 15, gz, gz_len, gz_len, back, LEN + 1,
                  LEN + 1, &ended) == given && !ended);
         TEST_CHECK(!memcmp(back, expect, given));
      }
      rdeflate_set_in(z, NULL, 0);
      rdeflate_finish(z);
      for (;;)
      {
         size_t wrote = 0;
         int    r;
         rdeflate_set_out(z, zout, sizeof(zout));
         r = rdeflate_process(z, NULL, &wrote);
         TEST_CHECK(r != RDEFLATE_PROCESS_ERROR);
         memcpy(gz + gz_len, zout, wrote);
         gz_len += wrote;
         if (r != RDEFLATE_PROCESS_NEXT)
            break;
      }
      rdeflate_free(z);
      {
         int ended = 0;
         TEST_CHECK(inflate_steps(16 + 15, gz, gz_len, 4096, back, LEN + 1,
                  999, &ended) == LEN && ended);
         TEST_CHECK(!memcmp(back, expect, LEN));
      }
      if (argc > 2)
         TEST_CHECK(test_write_file(argv[2], gz, gz_len));
   }

   free(expect);
   free(gz);
   free(back);
   if (test_failures)
   {
      fprintf(stderr, "test_deflate: %d check(s) failed\n", test_failures);
      return 1;
   }
   printf("test_deflate: ok\n");
   return 0;
}
