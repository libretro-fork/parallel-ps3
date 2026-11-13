/* A map from one 64-bit number to another that only ever gains entries, read
 * and added to by any number of threads with no lock.
 *
 * For a table something else already serialises the building of - the
 * writers hold whatever lock they held before - but that is looked up from
 * threads which should not have to wait for a writer: the reader's side is a
 * hash and a few loads.
 *
 * A fixed number of slots, a key and a value each, found by hashing the key
 * and walking on. A slot is claimed for a key with a compare-and-swap and
 * keeps it; the value is put in once and never changes (make it a pointer
 * to what does). Keys and values are not zero. */

#ifndef LF_U64_MAP_H
#define LF_U64_MAP_H

#include <stddef.h>
#include <stdint.h>

#include <retro_atomic.h>

#ifdef __cplusplus
extern "C" {
#endif

struct lf_u64_map
{
   retro_atomic_64_t *slots; /* key, value, key, value, ... */
   size_t mask;              /* slots less one */
};

/* Room for slots entries, a power of two. 0 when the memory is not to be
 * had. */
int lf_u64_map_init(struct lf_u64_map *map, size_t slots);

/* Not while anything uses the map. Safe on a zeroed map. */
void lf_u64_map_free(struct lf_u64_map *map);

/* The key's value, or 0 when it has none */
uint64_t lf_u64_map_get(const struct lf_u64_map *map, uint64_t key);

/* Gives the key this value unless it has one. Returns the value the key has
 * afterwards - this one, or the one that was there - or 0 when the map is
 * full. */
uint64_t lf_u64_map_add(struct lf_u64_map *map, uint64_t key, uint64_t value);

/* Calls fn for every key that has a value. Entries added while it runs may
 * or may not be seen. */
typedef void (*lf_u64_map_each_t)(void *ud, uint64_t key, uint64_t value);
void lf_u64_map_each(const struct lf_u64_map *map, lf_u64_map_each_t fn,
      void *ud);

#ifdef __cplusplus
}
#endif

#endif
