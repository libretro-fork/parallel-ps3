/* An object for each of some SPU instruction addresses: see pc_slots.h.
 * C89. */

#include "pc_slots.h"

#include <stdlib.h>
#include <string.h>

/* A slab's header takes one alignment unit, the objects follow it */
typedef struct pc_slots_slab
{
   struct pc_slots_slab *next;
} pc_slots_slab_t;

#define PC_SLOTS_SLAB_BYTES 0x10000u   /* about what a slab takes */

void pc_slots_init(pc_slots_t *t, size_t size)
{
   t->stride   = (uint32_t)((size + (PC_SLOTS_ALIGN - 1u)) & ~(size_t)(PC_SLOTS_ALIGN - 1u));
   t->per_slab = PC_SLOTS_SLAB_BYTES / t->stride;
   if (t->per_slab < 8u)
      t->per_slab = 8u;
}

static void *pc_slots_object(pc_slots_t *t)
{
   pc_slots_slab_t *slab = t->in_use;

   if (!slab || t->used == t->per_slab)
   {
      pc_slots_slab_t *next = slab ? slab->next : t->first;
      if (!next)
      {
         next = (pc_slots_slab_t*)malloc(PC_SLOTS_ALIGN + (size_t)t->stride * t->per_slab);
         if (!next)
            return NULL;
         next->next = NULL;
         if (slab)
            slab->next = next;
         else
            t->first = next;
      }
      t->in_use = slab = next;
      t->used   = 0;
   }

   return (char*)slab + PC_SLOTS_ALIGN + (size_t)t->stride * t->used++;
}

void *pc_slots_add(pc_slots_t *t, uint32_t pc)
{
   void ***slot, *object;
   uint32_t leaf;

   if (!pc_slots_valid(pc))
      return NULL;

   leaf = pc / 4u / PC_SLOTS_LEAF;
   slot = &t->leaf[leaf];
   if (!*slot)
   {
      *slot = (void**)calloc(PC_SLOTS_LEAF, sizeof(**slot));
      if (!*slot)
         return NULL;
   }

   object = pc_slots_object(t);
   if (!object)
      return NULL;

   (*slot)[(pc / 4u) % PC_SLOTS_LEAF] = object;
   t->leaf_live[leaf]++;
   t->count++;
   return object;
}

void *pc_slots_from(const pc_slots_t *t, uint32_t pc)
{
   uint32_t index, leaf, i;

   if (pc > PC_SLOTS_LAST)
      return NULL;

   index = (pc + 3u) / 4u;
   for (leaf = index / PC_SLOTS_LEAF, i = index % PC_SLOTS_LEAF; leaf < PC_SLOTS_LEAVES; leaf++, i = 0)
   {
      void **objects = t->leaf[leaf];
      if (!t->leaf_live[leaf])
         continue;
      for (; i < PC_SLOTS_LEAF; i++)
      {
         if (objects[i])
            return objects[i];
      }
   }
   return NULL;
}

void *pc_slots_before(const pc_slots_t *t, uint32_t pc)
{
   uint32_t index, leaf, i;

   if (!pc)
      return NULL;
   if (pc > PC_SLOTS_LAST)
      pc = PC_SLOTS_LAST + 4u;

   /* The last index whose address is below @pc */
   index = (pc - 1u) / 4u;
   leaf  = index / PC_SLOTS_LEAF;
   i     = index % PC_SLOTS_LEAF + 1u;
   for (;;)
   {
      void **objects = t->leaf[leaf];
      if (t->leaf_live[leaf])
      {
         while (i--)
         {
            if (objects[i])
               return objects[i];
         }
      }
      if (!leaf--)
         return NULL;
      i = PC_SLOTS_LEAF;
   }
}

void pc_slots_clear(pc_slots_t *t)
{
   uint32_t leaf;

   for (leaf = 0; leaf < PC_SLOTS_LEAVES; leaf++)
   {
      if (!t->leaf_live[leaf])
         continue;
      memset(t->leaf[leaf], 0, PC_SLOTS_LEAF * sizeof(void*));
      t->leaf_live[leaf] = 0;
   }
   t->count  = 0;
   t->in_use = NULL;
   t->used   = 0;
}

void pc_slots_free(pc_slots_t *t)
{
   const uint32_t stride = t->stride, per_slab = t->per_slab;
   pc_slots_slab_t *slab = t->first;
   uint32_t leaf;

   while (slab)
   {
      pc_slots_slab_t *next = slab->next;
      free(slab);
      slab = next;
   }
   for (leaf = 0; leaf < PC_SLOTS_LEAVES; leaf++)
      free(t->leaf[leaf]);

   memset(t, 0, sizeof(*t));
   t->stride   = stride;
   t->per_slab = per_slab;
}
