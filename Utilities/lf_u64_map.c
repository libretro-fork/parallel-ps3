/* See lf_u64_map.h */

#include "lf_u64_map.h"

#include <stdlib.h>

static size_t lf_start(uint64_t key)
{
   /* Fibonacci hashing: the high bits are the well mixed ones */
   return (size_t)((key * 0x9E3779B97F4A7C15ull) >> 20);
}

int lf_u64_map_init(struct lf_u64_map *map, size_t slots)
{
   map->slots = (retro_atomic_64_t*)calloc(slots, 2 * sizeof(*map->slots));
   map->mask  = map->slots ? slots - 1 : 0;
   return map->slots != NULL;
}

void lf_u64_map_free(struct lf_u64_map *map)
{
   free(map->slots);
   map->slots = NULL;
   map->mask  = 0;
}

uint64_t lf_u64_map_get(const struct lf_u64_map *map, uint64_t key)
{
   size_t at = lf_start(key);
   size_t n;

   if (!map->slots)
      return 0;

   for (n = 0; n <= map->mask; n++)
   {
      retro_atomic_64_t *slot = &map->slots[2 * ((at + n) & map->mask)];
      uint64_t have = (uint64_t)retro_atomic_load_acquire_64(slot);

      if (!have)
         return 0;

      /* A key whose value is not in yet reads as having none: its adder
       * has not returned */
      if (have == key)
         return (uint64_t)retro_atomic_load_acquire_64(slot + 1);
   }

   return 0;
}

uint64_t lf_u64_map_add(struct lf_u64_map *map, uint64_t key, uint64_t value)
{
   size_t at = lf_start(key);
   size_t n;

   if (!map->slots)
      return 0;

   for (n = 0; n <= map->mask; n++)
   {
      retro_atomic_64_t *slot = &map->slots[2 * ((at + n) & map->mask)];
      uint64_t have = (uint64_t)retro_atomic_load_acquire_64(slot);

      if (!have)
      {
         if (retro_atomic_cas_64(slot, 0, (int64_t)key))
            have = key;
         else
            have = (uint64_t)retro_atomic_load_acquire_64(slot);
      }

      if (have == key)
      {
         /* The first value put in stays */
         if (retro_atomic_cas_64(slot + 1, 0, (int64_t)value))
            return value;
         return (uint64_t)retro_atomic_load_acquire_64(slot + 1);
      }
   }

   return 0;
}

void lf_u64_map_each(const struct lf_u64_map *map, lf_u64_map_each_t fn,
      void *ud)
{
   size_t n;

   if (!map->slots)
      return;

   for (n = 0; n <= map->mask; n++)
   {
      uint64_t key   = (uint64_t)retro_atomic_load_acquire_64(&map->slots[2 * n]);
      uint64_t value = key
         ? (uint64_t)retro_atomic_load_acquire_64(&map->slots[2 * n + 1]) : 0;

      if (value)
         fn(ud, key, value);
   }
}
