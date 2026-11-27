/* A few values by address, sorted in one array: see addr_sizes.h. C89. */

#include "addr_sizes.h"

#include <stdlib.h>
#include <string.h>

uint32_t addr_sizes_add(addr_sizes_t *l, uint32_t addr, uint32_t size, int *added)
{
   const uint32_t at = addr_sizes_lower(l, addr);
   addr_size_t *e    = addr_sizes_data(l);

   if (at < l->count && e[at].addr == addr)
   {
      *added = 0;
      return at;
   }

   if (l->count == (l->heap ? l->cap : ADDR_SIZES_INLINE))
   {
      const uint32_t cap = l->heap ? l->cap * 2u : 8u;
      addr_size_t *heap;

      if (cap <= l->count)
         return 0xFFFFFFFFu;
      heap = (addr_size_t*)realloc(l->heap, (size_t)cap * sizeof(*heap));
      if (!heap)
         return 0xFFFFFFFFu;
      if (!l->heap)
         memcpy(heap, l->in_place, sizeof(l->in_place));
      l->heap = heap;
      l->cap  = cap;
      e       = heap;
   }

   memmove(e + at + 1, e + at, (size_t)(l->count - at) * sizeof(*e));
   e[at].addr = addr;
   e[at].size = size;
   l->count++;
   *added = 1;
   return at;
}

int addr_sizes_copy(addr_sizes_t *dst, const addr_sizes_t *src)
{
   if (dst == src)
      return 1;

   if (src->count > (dst->heap ? dst->cap : ADDR_SIZES_INLINE))
   {
      /* Room for exactly what there is: most copies are never added to */
      addr_size_t *heap = (addr_size_t*)malloc((size_t)src->count * sizeof(*heap));
      if (!heap)
         return 0;
      free(dst->heap);
      dst->heap = heap;
      dst->cap  = src->count;
   }

   memcpy(addr_sizes_data(dst), addr_sizes_cdata(src), (size_t)src->count * sizeof(addr_size_t));
   dst->count = src->count;
   return 1;
}

void addr_sizes_move(addr_sizes_t *dst, addr_sizes_t *src)
{
   if (dst == src)
      return;

   free(dst->heap);
   *dst = *src;   /* in place: the pairs came with it; on the heap: the pointer */
   src->heap  = NULL;
   src->count = 0;
   src->cap   = 0;
}

void addr_sizes_free(addr_sizes_t *l)
{
   free(l->heap);
   l->heap  = NULL;
   l->count = 0;
   l->cap   = 0;
}
