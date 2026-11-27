/* A word for each 4 KiB page of a 32-bit address space
 * (rpcs3/util/page_words.c), where the Z-cull reports' locked pages were
 * hash maps:
 *
 * - a page never asked for is found as none, or as a zero word once its
 *   leaf is there; asking for it gives a zero word and allocates the leaf
 *   once;
 * - every address of a page gives that page's word, the pages on either
 *   side their own, the first and last pages of the address space too;
 * - a word keeps what is written to it, and stays where it is as other
 *   leaves are allocated;
 * - the address worked back from a leaf and an index is the page's;
 * - freed: empty, and usable again. */

#include "../../util/page_words.h"
#include "test_common.h"

#include <stdio.h>
#include <string.h>

int main(void)
{
   static page_words_t p;
   uint32_t *w, *w2, *first, *last;
   uint32_t leaf, i, found;

   memset(&p, 0, sizeof(p));
   TEST_CHECK(page_words_find(&p, 0xC0100000u) == NULL);

   w = page_words_get(&p, 0xC0100000u);
   TEST_CHECK(w != NULL && *w == 0);
   if (!w)
      return 1;
   *w = 0x80000003u;

   /* The whole page is that word; its neighbours have their own */
   TEST_CHECK(page_words_find(&p, 0xC0100000u) == w && page_words_find(&p, 0xC0100FFFu) == w);
   TEST_CHECK(page_words_get(&p, 0xC0100ABCu) == w && *w == 0x80000003u);
   w2 = page_words_find(&p, 0xC0101000u);
   TEST_CHECK(w2 == w + 1 && *w2 == 0);
   w2 = page_words_find(&p, 0xC00FFFFFu);
   TEST_CHECK(w2 == w - 1 && *w2 == 0);

   /* Another leaf: none until asked for; the first word did not move */
   TEST_CHECK(page_words_find(&p, 0xC0500000u) == NULL);
   w2 = page_words_get(&p, 0xC0500000u);
   TEST_CHECK(w2 != NULL && *w2 == 0 && w2 != w);
   TEST_CHECK(page_words_find(&p, 0xC0100000u) == w && *w == 0x80000003u);

   /* The ends of the address space */
   first = page_words_get(&p, 0);
   last = page_words_get(&p, 0xFFFFFFFFu);
   TEST_CHECK(first != NULL && last != NULL && first != last);
   if (!first || !last)
      return 1;
   *first = 1;
   *last = 2;
   TEST_CHECK(page_words_find(&p, 0xFFFu) == first && page_words_find(&p, 0xFFFFF000u) == last);
   TEST_CHECK(p.leaf[0] == first && p.leaf[PAGE_WORDS_LEAVES - 1] + (PAGE_WORDS_LEAF_PAGES - 1) == last);

   /* The address back from where a word is */
   found = 0;
   for (leaf = 0; leaf < PAGE_WORDS_LEAVES; leaf++)
   {
      for (i = 0; p.leaf[leaf] && i < PAGE_WORDS_LEAF_PAGES; i++)
      {
         if (!p.leaf[leaf][i])
            continue;
         found++;
         TEST_CHECK(page_words_find(&p, page_words_addr(leaf, i)) == &p.leaf[leaf][i]);
         if (p.leaf[leaf][i] == 0x80000003u)
            TEST_CHECK(page_words_addr(leaf, i) == 0xC0100000u);
         if (p.leaf[leaf][i] == 2)
            TEST_CHECK(page_words_addr(leaf, i) == 0xFFFFF000u);
      }
   }
   TEST_CHECK(found == 3);

   page_words_free(&p);
   TEST_CHECK(page_words_find(&p, 0xC0100000u) == NULL && page_words_find(&p, 0) == NULL);
   w = page_words_get(&p, 0xC0100000u);
   TEST_CHECK(w != NULL && *w == 0);
   page_words_free(&p);

   if (test_failures)
   {
      fprintf(stderr, "test_page_words: %d failures\n", test_failures);
      return 1;
   }
   printf("test_page_words: ok\n");
   return 0;
}
