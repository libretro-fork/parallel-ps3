/* A word for each page of a 32-bit address space: see page_words.h. C89. */

#include "page_words.h"

#include <stdlib.h>
#include <string.h>

uint32_t *page_words_get(page_words_t *p, uint32_t addr)
{
   uint32_t **slot = &p->leaf[addr >> 22];
   if (!*slot)
   {
      *slot = (uint32_t*)calloc(PAGE_WORDS_LEAF_PAGES, sizeof(uint32_t));
      if (!*slot)
         return NULL;
   }
   return &(*slot)[(addr >> 12) & (PAGE_WORDS_LEAF_PAGES - 1)];
}

void page_words_free(page_words_t *p)
{
   size_t i;
   for (i = 0; i < PAGE_WORDS_LEAVES; i++)
      free(p->leaf[i]);
   memset(p, 0, sizeof(*p));
}
