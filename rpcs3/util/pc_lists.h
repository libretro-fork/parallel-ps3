#ifndef RPCS3_UTIL_PC_LISTS_H
#define RPCS3_UTIL_PC_LISTS_H

/* A list of 32-bit values for each instruction address of an SPU's local
 * storage: the SPU analyser's branch targets of a block and predecessors
 * of a block (Emu/Cell/SPURecompiler.h). They were
 * std::unordered_map<u32, std::vector<u32>>.
 *
 * An address is found by itself: its entry is one of 256 in the leaf for
 * its kilobyte, a leaf allocated when one of its addresses is first used
 * and kept. So an entry stays where it is for good - a pointer to it or
 * to its list holds while others are added and taken out, as a reference
 * into the map did - and going through them goes by rising address, the
 * same every run. Clearing keeps the leaves and the lists' memory: after
 * the first programs nothing is allocated. One thread at a time. C89.
 *
 * Addresses are multiples of 4 from 0 to PC_LISTS_LAST. Plain data:
 * zeroed, a table is empty; freed, it is empty again. */

#include <stddef.h>
#include <stdint.h>

#include <retro_inline.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PC_LISTS_LEAF   256u                         /* entries in a leaf */
#define PC_LISTS_LAST   0x40000u                     /* the end of local storage: a target, never code */
#define PC_LISTS_LEAVES (PC_LISTS_LAST / 4u / PC_LISTS_LEAF + 1u)

typedef struct pc_list
{
   uint32_t *data;
   uint32_t  count;
   uint32_t  cap;
} pc_list_t;

typedef struct pc_list_entry
{
   uint32_t  pc;
   uint32_t  live;   /* in the table (an entry can be there with an empty list) */
   pc_list_t list;
} pc_list_entry_t;

typedef struct pc_lists
{
   pc_list_entry_t *leaf[PC_LISTS_LEAVES];
   uint16_t         leaf_live[PC_LISTS_LEAVES];   /* live entries in each leaf */
   uint32_t         count;                        /* live entries */
} pc_lists_t;

/* Whether @pc can be in a table */
static INLINE int pc_lists_valid(uint32_t pc)
{
   return pc <= PC_LISTS_LAST && !(pc & 3u);
}

/* @pc's entry, or NULL where it is not in the table (or cannot be) */
static INLINE pc_list_entry_t *pc_lists_find(const pc_lists_t *t, uint32_t pc)
{
   pc_list_entry_t *leaf;
   if (!pc_lists_valid(pc))
      return NULL;
   leaf = t->leaf[pc / 4u / PC_LISTS_LEAF];
   if (!leaf)
      return NULL;
   leaf += (pc / 4u) % PC_LISTS_LEAF;
   return leaf->live ? leaf : NULL;
}

/* @pc's entry, put in with an empty list where it was not there. NULL:
 * out of memory, or @pc cannot be in a table */
pc_list_entry_t *pc_lists_get(pc_lists_t *t, uint32_t pc);

/* @e taken out, its list emptied (the list's memory is kept for the
 * address's next use) */
void pc_lists_erase(pc_lists_t *t, pc_list_entry_t *e);

/* The first entry at or after @pc by rising address, or NULL. To go
 * through all: from 0, then from each one's pc + 4 */
pc_list_entry_t *pc_lists_from(const pc_lists_t *t, uint32_t pc);

/* Every entry taken out; leaves and lists' memory kept */
void pc_lists_clear(pc_lists_t *t);

/* Everything freed: empty, and usable again */
void pc_lists_free(pc_lists_t *t);

/* Room for @cap values in all. 0: out of memory, the list unchanged */
int pc_list_reserve(pc_list_t *l, uint32_t cap);

/* @value added at the end. 0: out of memory */
static INLINE int pc_list_push(pc_list_t *l, uint32_t value)
{
   if (l->count == l->cap && !pc_list_reserve(l, l->cap ? l->cap * 2u : 4u))
      return 0;
   l->data[l->count++] = value;
   return 1;
}

#ifdef __cplusplus
}
#endif

#endif
