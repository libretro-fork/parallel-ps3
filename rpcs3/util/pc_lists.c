/* A list of values for each SPU instruction address: see pc_lists.h. C89. */

#include "pc_lists.h"

#include <stdlib.h>
#include <string.h>

pc_list_entry_t *pc_lists_get(pc_lists_t *t, uint32_t pc)
{
   pc_list_entry_t **slot, *e;
   uint32_t leaf;

   if (!pc_lists_valid(pc))
      return NULL;

   leaf = pc / 4u / PC_LISTS_LEAF;
   slot = &t->leaf[leaf];
   if (!*slot)
   {
      *slot = (pc_list_entry_t*)calloc(PC_LISTS_LEAF, sizeof(**slot));
      if (!*slot)
         return NULL;
   }

   e = *slot + (pc / 4u) % PC_LISTS_LEAF;
   if (!e->live)
   {
      e->pc = pc;
      e->live = 1;
      e->list.count = 0;
      t->leaf_live[leaf]++;
      t->count++;
   }
   return e;
}

void pc_lists_erase(pc_lists_t *t, pc_list_entry_t *e)
{
   if (!e->live)
      return;
   e->live = 0;
   e->list.count = 0;
   t->leaf_live[e->pc / 4u / PC_LISTS_LEAF]--;
   t->count--;
}

pc_list_entry_t *pc_lists_from(const pc_lists_t *t, uint32_t pc)
{
   uint32_t index, leaf, i;

   if (pc > PC_LISTS_LAST)
      return NULL;

   index = (pc + 3u) / 4u;
   for (leaf = index / PC_LISTS_LEAF, i = index % PC_LISTS_LEAF; leaf < PC_LISTS_LEAVES; leaf++, i = 0)
   {
      const pc_list_entry_t *entries = t->leaf[leaf];
      if (!t->leaf_live[leaf])
         continue;
      for (; i < PC_LISTS_LEAF; i++)
      {
         if (entries[i].live)
            return t->leaf[leaf] + i;
      }
   }
   return NULL;
}

void pc_lists_clear(pc_lists_t *t)
{
   uint32_t leaf, i;

   for (leaf = 0; leaf < PC_LISTS_LEAVES; leaf++)
   {
      pc_list_entry_t *entries = t->leaf[leaf];
      if (!t->leaf_live[leaf])
         continue;
      for (i = 0; i < PC_LISTS_LEAF; i++)
      {
         entries[i].live = 0;
         entries[i].list.count = 0;
      }
      t->leaf_live[leaf] = 0;
   }
   t->count = 0;
}

void pc_lists_free(pc_lists_t *t)
{
   uint32_t leaf, i;

   for (leaf = 0; leaf < PC_LISTS_LEAVES; leaf++)
   {
      pc_list_entry_t *entries = t->leaf[leaf];
      if (!entries)
         continue;
      for (i = 0; i < PC_LISTS_LEAF; i++)
         free(entries[i].list.data);
      free(entries);
   }
   memset(t, 0, sizeof(*t));
}

int pc_list_reserve(pc_list_t *l, uint32_t cap)
{
   uint32_t *data;

   if (cap <= l->cap)
      return 1;
   data = (uint32_t*)realloc(l->data, (size_t)cap * sizeof(*data));
   if (!data)
      return 0;
   l->data = data;
   l->cap = cap;
   return 1;
}
