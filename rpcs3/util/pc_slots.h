#ifndef RPCS3_UTIL_PC_SLOTS_H
#define RPCS3_UTIL_PC_SLOTS_H

/* An object for some of the instruction addresses of an SPU's local
 * storage, kept in address order: the SPU analyser's basic blocks,
 * functions and patterns (Emu/Cell/SPURecompiler.h). They were
 * std::map<u32, T>: a node allocated for each, found by walking a tree.
 *
 * An address's object is found by itself: its pointer is one of 256 in
 * the leaf for its kilobyte. The objects are of one size, cut from slabs
 * (a small one first, then larger) and never moved, so a pointer to one holds while others are added, as
 * a reference into the map did. Clearing keeps the leaves and the slabs:
 * after the first programs nothing is allocated. The table does not look
 * inside an object; whoever owns it builds one where it is handed out
 * and finishes with every one before clearing. Nothing is taken out
 * singly. One thread at a time. C89.
 *
 * Addresses are multiples of 4 from 0 to PC_SLOTS_LAST. Zeroed and then
 * given its object size, a table is empty; freed, it is empty again and
 * keeps the size. */

#include <stddef.h>
#include <stdint.h>

#include <retro_inline.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PC_SLOTS_LEAF   256u                         /* pointers in a leaf */
#define PC_SLOTS_LAST   0x40000u                     /* the end of local storage */
#define PC_SLOTS_LEAVES (PC_SLOTS_LAST / 4u / PC_SLOTS_LEAF + 1u)
#define PC_SLOTS_ALIGN  16u                          /* what an object is aligned to */

struct pc_slots_slab;

typedef struct pc_slots
{
   void                **leaf[PC_SLOTS_LEAVES];
   uint16_t              leaf_live[PC_SLOTS_LEAVES];   /* objects in each leaf */
   uint32_t              count;                        /* objects */
   uint32_t              stride;                       /* an object's size, rounded up to the alignment */
   uint32_t              per_slab;                     /* objects in a full-size slab */
   uint32_t              used;                         /* objects handed out of the slab in use */
   struct pc_slots_slab *first;                        /* every slab, in the order they fill */
   struct pc_slots_slab *in_use;                       /* the one being handed out of */
} pc_slots_t;

/* Sets the size of the objects of a zeroed table. @size is not 0 */
void pc_slots_init(pc_slots_t *t, size_t size);

/* Whether @pc can be in a table */
static INLINE int pc_slots_valid(uint32_t pc)
{
   return pc <= PC_SLOTS_LAST && !(pc & 3u);
}

/* @pc's object, or NULL where it has none (or cannot have) */
static INLINE void *pc_slots_find(const pc_slots_t *t, uint32_t pc)
{
   void **leaf;
   if (!pc_slots_valid(pc))
      return NULL;
   leaf = t->leaf[pc / 4u / PC_SLOTS_LEAF];
   return leaf ? leaf[(pc / 4u) % PC_SLOTS_LEAF] : NULL;
}

/* Memory for an object for @pc, which has none: put in, its content
 * whatever was there before. NULL: out of memory, or @pc cannot be in a
 * table */
void *pc_slots_add(pc_slots_t *t, uint32_t pc);

/* The object of the first address at or after @pc, or NULL */
void *pc_slots_from(const pc_slots_t *t, uint32_t pc);

/* The object of the last address before @pc, or NULL */
void *pc_slots_before(const pc_slots_t *t, uint32_t pc);

/* Every object dropped as it is; leaves and slabs kept */
void pc_slots_clear(pc_slots_t *t);

/* Everything freed: empty, usable again, the object size kept */
void pc_slots_free(pc_slots_t *t);

#ifdef __cplusplus
}
#endif

#endif
