/* VBlank Frequency "From List" (libretro/vblank_list.c): the block of
 * vblank_unlock.txt a game is in.
 *
 * - every title ID of the list the core carries is found in its block,
 *   read again with the file's lines ended by CR LF as an editor on
 *   Windows saves it
 * - a title ID under another block, before the first, a prefix of a
 *   listed one, or in a comment is not; a comment after one is no part
 *   of it; the last line needs no line end
 *
 * Usage: test_vblank_list vblank_unlock.txt */

#include "../vblank_list.h"
#include "test_common.h"

static const char doc[] =
   "BLUS99999\n"
   "[30 fps]\n"
   "  BLUS30443   # Demon's Souls\r\n"
   "# BLES00932\n"
   "\tBLJM60180\t\r\n"
   "[other]\n"
   "BCES00001\n"
   "[60 fps]\n"
   "NPUB30000#no space\n"
   "BLES01265";

static unsigned block_of(const char *id)
{
   return vblank_list_block(doc, sizeof(doc) - 1, id);
}

int main(int argc, char **argv)
{
   uint8_t *data;
   char *crlf;
   size_t len = 0, crlf_len = 0, i, start, entries = 0, misses = 0;
   unsigned block = 0;

   TEST_CHECK(block_of("BLUS30443") == 30);
   TEST_CHECK(block_of("BLJM60180") == 30);
   TEST_CHECK(block_of("NPUB30000") == 60);
   TEST_CHECK(block_of("BLES01265") == 60);
   TEST_CHECK(block_of("BLUS99999") == 0);
   TEST_CHECK(block_of("BLES00932") == 0);
   TEST_CHECK(block_of("BCES00001") == 0);
   TEST_CHECK(block_of("BLUS3044") == 0);
   TEST_CHECK(block_of("") == 0);

   if (argc < 2 || !(data = test_read_file(argv[1], &len)))
   {
      fprintf(stderr, "cannot read the list\n");
      return 1;
   }

   /* The same with CR LF */
   crlf = (char*)malloc(len * 2 + 1);
   for (i = 0; i < len; i++)
   {
      if (data[i] == '\n')
         crlf[crlf_len++] = '\r';
      crlf[crlf_len++] = (char)data[i];
   }

   /* Each title ID line, by its own reading: a block line starts with
    * '[', an ID is the line's first word when it starts with a letter */
   for (start = 0; start < len; start = i + 1)
   {
      char id[32];
      size_t n = 0;
      for (i = start; i < len && data[i] != '\n'; i++)
         ;
      if (data[start] == '[')
         block = data[start + 1] == '3' ? 30 : data[start + 1] == '6' ? 60 : 0;
      else if (block && data[start] >= 'A' && data[start] <= 'Z')
      {
         while (start + n < i && n < sizeof(id) - 1 && data[start + n] != ' '
               && data[start + n] != '\t' && data[start + n] != '\r' && data[start + n] != '#')
         {
            id[n] = (char)data[start + n];
            n++;
         }
         id[n] = '\0';
         entries++;
         if (vblank_list_block((const char*)data, len, id) != block
               || vblank_list_block(crlf, crlf_len, id) != block)
         {
            if (misses++ < 5)
               fprintf(stderr, "%s: not found under [%u\n", id, block);
         }
      }
   }

   TEST_CHECK(entries > 300);
   TEST_CHECK(misses == 0);
   printf("vblank list: %u title IDs, %u not in their block\n",
         (unsigned)entries, (unsigned)misses);
   free(crlf);
   free(data);

   if (test_failures)
      printf("vblank list: FAIL\n");
   return test_failures ? 1 : 0;
}
