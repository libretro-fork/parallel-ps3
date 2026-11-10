/* Shared by the library replacement tests: the content generator the
 * fixtures were made from, and small helpers. C89. */

#ifndef RPCS3_LIBRETRO_TEST_COMMON_H
#define RPCS3_LIBRETRO_TEST_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static int test_failures = 0;

#define TEST_CHECK(cond) do { if (!(cond)) { \
   fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
   test_failures++; } } while (0)

/* Three 4 KiB runs of text, then one of random letters, over and over:
 * the fixtures in fixtures/ hold this compressed by the reference
 * libraries, from the same seeds. */
static void test_gen(uint8_t *out, size_t n, uint32_t seed)
{
   static const char text[] =
      "RPCS3 savestate chunk: the quick brown fox jumps over the lazy dog. ";
   const size_t text_len = sizeof(text) - 1;
   uint32_t x = seed;
   size_t i;
   for (i = 0; i < n; i++)
   {
      if ((i / 4096) % 4 != 0)
         out[i] = (uint8_t)text[i % text_len];
      else
      {
         x = x * 1103515245u + 12345u;
         out[i] = (uint8_t)(((x >> 16) & 0x03) | 0x40);
      }
   }
}

/* The whole file, for a fixture; NULL when it cannot be read */
static uint8_t *test_read_file(const char *path, size_t *len)
{
   FILE *f = fopen(path, "rb");
   uint8_t *buf = NULL;
   long n;
   if (!f)
      return NULL;
   if (fseek(f, 0, SEEK_END) == 0 && (n = ftell(f)) > 0
         && fseek(f, 0, SEEK_SET) == 0
         && (buf = (uint8_t*)malloc((size_t)n)))
   {
      if (fread(buf, 1, (size_t)n, f) != (size_t)n)
      {
         free(buf);
         buf = NULL;
      }
      else
         *len = (size_t)n;
   }
   fclose(f);
   return buf;
}

static int test_write_file(const char *path, const uint8_t *buf, size_t len)
{
   FILE *f = fopen(path, "wb");
   int ok;
   if (!f)
      return 0;
   ok = fwrite(buf, 1, len, f) == len;
   return fclose(f) == 0 && ok;
}

#endif
