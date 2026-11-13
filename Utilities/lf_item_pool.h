/* A pool of spare things - each with a key, a set of flag bits, a weight -
 * put in and taken out by any number of threads with no lock.
 *
 * For things kept for reuse once nothing uses them: whoever lets go of one
 * puts it in, whoever needs one like it takes it out, and from time to time
 * the oldest are thrown away.
 *
 * A fixed number of slots. A slot's key word is 0 when it is empty, 1
 * while one thread fills or empties it, and the key (plus 2) of the item in
 * it otherwise; a thread that swaps that word to 1 has the slot to itself.
 * The key words are an array of their own and the slot filled is the one
 * last emptied or else the lowest empty one, so looking for a key is one plain load per slot over about as
 * many slots as there are items - the pool knows how far up its slots have
 * anything in them - and nothing is written until one is found. */

#ifndef LF_ITEM_POOL_H
#define LF_ITEM_POOL_H

#include <stddef.h>
#include <stdint.h>

#include <retro_atomic.h>

#ifdef __cplusplus
extern "C" {
#endif

struct lf_item_pool_slot
{
   retro_atomic_64_t seq;  /* when the item was put: higher is newer */
   retro_atomic_64_t weight;
   uint64_t flags;         /* these two only its owner touches */
   void *item;
};

struct lf_item_pool
{
   retro_atomic_64_t *keys;    /* 0 empty, 1 owned by a thread, else key + 2 */
   struct lf_item_pool_slot *slots;
   uint64_t *scratch;          /* the trimmer's */
   size_t count;
   retro_atomic_64_t limit;    /* no slot from here up has anything in it */
   retro_atomic_size_t hint;   /* the slot last emptied: tried first */
   retro_atomic_64_t seq;
   retro_atomic_int_t closed;
   retro_atomic_int_t trimming;
};

typedef void (*lf_item_pool_drop_t)(void *ud, void *item);

/* Room for count items. 0 when the memory is not to be had. */
int lf_item_pool_init(struct lf_item_pool *pool, size_t count);

/* Not while anything uses the pool, and after it is emptied. Safe on a
 * zeroed pool. */
void lf_item_pool_free(struct lf_item_pool *pool);

/* Puts the item in. 0, and the item is still the caller's, when the pool
 * is full or closed (or the key is one of the two highest numbers). */
int lf_item_pool_put(struct lf_item_pool *pool, uint64_t key,
      uint64_t flags, uint64_t weight, void *item);

/* Takes out an item with this key that has every one of these flag bits.
 * NULL when there is none. */
void *lf_item_pool_take(struct lf_item_pool *pool, uint64_t key,
      uint64_t flags);

/* Takes out any item. NULL when the pool is empty. */
void *lf_item_pool_take_any(struct lf_item_pool *pool);

/* Takes out the oldest items until keep are left and hands each to drop.
 * Returns how many. One thread at a time: a second call while one runs
 * does nothing. */
size_t lf_item_pool_trim(struct lf_item_pool *pool, size_t keep,
      lf_item_pool_drop_t drop, void *ud);

/* From here on nothing can be put in: an item whose put returned 1 is one
 * that a take after this call finds. For emptying the pool for good. */
void lf_item_pool_close(struct lf_item_pool *pool);

/* Items in the pool, and their weights added up, about now. Counted when
 * asked - putting and taking keep no totals - so for once a frame, not
 * for every item. */
size_t lf_item_pool_held(const struct lf_item_pool *pool);
uint64_t lf_item_pool_weight(const struct lf_item_pool *pool);

#ifdef __cplusplus
}
#endif

#endif
