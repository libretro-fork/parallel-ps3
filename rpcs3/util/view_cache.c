/* A frame's image views by address and properties: see view_cache.h. C89. */

#include "view_cache.h"

#include <stdlib.h>
#include <string.h>

static uint32_t slot_of(uint32_t address, uint64_t props, uint32_t mask)
{
   uint64_t k = props ^ ((uint64_t)address << 13) ^ address;
   k ^= k >> 33;
   k *= ((uint64_t)0xFF51AFD7u << 32) | 0xED558CCDu;
   k ^= k >> 33;
   return (uint32_t)k & mask;
}

view_cache_entry_t *view_cache_find(const view_cache_t *c, uint32_t address, uint64_t props)
{
   uint32_t mask, i;

   if (!c->live)
      return NULL;

   mask = c->cap - 1;
   for (i = slot_of(address, props, mask); c->e[i].view; i = (i + 1) & mask)
   {
      view_cache_entry_t *e = &c->e[i];
      if (e->address == address && e->props == props && e->view != VIEW_CACHE_DEAD)
         return e;
   }
   return NULL;
}

/* @src's live entries into a table of @cap, which takes its place */
static int rebuild(view_cache_t *c, uint32_t cap)
{
   view_cache_entry_t *e = (view_cache_entry_t*)calloc(cap, sizeof(*e));
   uint32_t mask = cap - 1, i, j;

   if (!e)
      return 0;
   for (i = 0; i < c->cap; i++)
   {
      const view_cache_entry_t *old = &c->e[i];
      if (!VIEW_CACHE_LIVE(old))
         continue;
      for (j = slot_of(old->address, old->props, mask); e[j].view; j = (j + 1) & mask)
         ;
      e[j] = *old;
   }
   free(c->e);
   c->e = e;
   c->cap = cap;
   c->used = c->live;
   return 1;
}

int view_cache_add(view_cache_t *c, uint32_t address, uint64_t props, uint32_t remap,
      uint32_t range_start, uint32_t range_end, void *view)
{
   view_cache_entry_t *e;
   uint32_t mask, i;

   if (!view || view == VIEW_CACHE_DEAD)
      return 0;

   /* At most half of it not empty. Dead entries count: a table full of
    * them is rebuilt at the same size, a table full of live ones doubles */
   if ((c->used + 1) * 2 > c->cap)
   {
      uint32_t cap = c->cap ? c->cap : 32;
      while ((c->live + 1) * 2 > cap)
         cap *= 2;
      if (!rebuild(c, cap))
         return 0;
   }

   mask = c->cap - 1;
   for (i = slot_of(address, props, mask); c->e[i].view; i = (i + 1) & mask)
      ;
   e = &c->e[i];
   e->props = props;
   e->address = address;
   e->remap = remap;
   e->range_start = range_start;
   e->range_end = range_end;
   e->view = view;
   c->used++;
   c->live++;
   return 1;
}

void view_cache_kill(view_cache_t *c, view_cache_entry_t *e)
{
   e->view = VIEW_CACHE_DEAD;
   c->live--;
}

void view_cache_clear(view_cache_t *c)
{
   if (c->used)
      memset(c->e, 0, (size_t)c->cap * sizeof(*c->e));
   c->used = 0;
   c->live = 0;
}

void view_cache_free(view_cache_t *c)
{
   free(c->e);
   memset(c, 0, sizeof(*c));
}
