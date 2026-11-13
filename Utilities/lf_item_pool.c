/* See lf_item_pool.h */

#include "lf_item_pool.h"

#include <stdlib.h>

#define LF_POOL_EMPTY 0
#define LF_POOL_OWNED 1
#define LF_POOL_BIAS  2

int lf_item_pool_init(struct lf_item_pool *pool, size_t count)
{
   pool->keys    = (retro_atomic_64_t*)calloc(count, sizeof(*pool->keys));
   pool->slots   = (struct lf_item_pool_slot*)calloc(count, sizeof(*pool->slots));
   pool->scratch = (uint64_t*)calloc(count, sizeof(*pool->scratch));

   if (!pool->keys || !pool->slots || !pool->scratch)
   {
      lf_item_pool_free(pool);
      return 0;
   }

   pool->count = count;
   retro_atomic_store_release_64(&pool->limit, 0);
   retro_atomic_store_release_size(&pool->hint, 0);
   retro_atomic_store_release_64(&pool->seq, 0);
   retro_atomic_store_release_int(&pool->closed, 0);
   retro_atomic_store_release_int(&pool->trimming, 0);
   return 1;
}

void lf_item_pool_free(struct lf_item_pool *pool)
{
   free(pool->keys);
   free(pool->slots);
   free(pool->scratch);
   pool->keys    = NULL;
   pool->slots   = NULL;
   pool->scratch = NULL;
   pool->count   = 0;
}

/* The limit is at least this from here on */
static void lf_pool_raise(struct lf_item_pool *pool, size_t limit)
{
   uint64_t have = (uint64_t)retro_atomic_load_acquire_64(&pool->limit);

   while (have < limit && !retro_atomic_cas_64(&pool->limit, have, limit))
      have = (uint64_t)retro_atomic_load_acquire_64(&pool->limit);
}

/* The owner of slot i takes its item out and gives the slot up.
 *
 * When it is the top slot the limit comes down past it, and past the empty
 * slots under it - each one while this thread owns it. A slot the limit
 * has come down past can so only be claimed by a thread that puts
 * something in after the limit moved, and that thread, which raises the
 * limit once it has the slot, sees where it is: no item is ever at or
 * above the limit. */
static void *lf_pool_empty(struct lf_item_pool *pool, size_t i)
{
   struct lf_item_pool_slot *slot = &pool->slots[i];
   void *item = slot->item;

   slot->item = NULL;
   retro_atomic_store_relaxed_size(&pool->hint, i);

   for (;;)
   {
      int lowered = (uint64_t)retro_atomic_load_relaxed_64(&pool->limit) == i + 1
         && retro_atomic_cas_64(&pool->limit, i + 1, i);

      retro_atomic_store_release_64(&pool->keys[i], LF_POOL_EMPTY);

      if (!lowered || !i)
         break;

      i--;

      if (retro_atomic_load_relaxed_64(&pool->keys[i]) != LF_POOL_EMPTY
            || !retro_atomic_cas_64(&pool->keys[i], LF_POOL_EMPTY, LF_POOL_OWNED))
         break;
   }

   return item;
}

int lf_item_pool_put(struct lf_item_pool *pool, uint64_t key,
      uint64_t flags, uint64_t weight, void *item)
{
   size_t i, n, hint;

   if (key > ~(uint64_t)0 - LF_POOL_BIAS)
      return 0;

   if (retro_atomic_load_acquire_int(&pool->closed))
      return 0;

   /* The slot last emptied, then from the bottom up */
   hint = retro_atomic_load_relaxed_size(&pool->hint);

   for (n = 0; n <= pool->count; n++)
   {
      struct lf_item_pool_slot *slot;
      retro_atomic_64_t *word;

      if (n == 0)
      {
         if (hint >= pool->count)
            continue;
         i = hint;
      }
      else
         i = n - 1;

      slot = &pool->slots[i];
      word = &pool->keys[i];

      if (retro_atomic_load_relaxed_64(word) != LF_POOL_EMPTY)
         continue;

      if (!retro_atomic_cas_64(word, LF_POOL_EMPTY, LF_POOL_OWNED))
         continue;

      lf_pool_raise(pool, i + 1);

      slot->flags = flags;
      slot->item  = item;
      retro_atomic_store_relaxed_64(&slot->weight, weight);
      retro_atomic_store_relaxed_64(&slot->seq,
            (uint64_t)retro_atomic_fetch_add_64(&pool->seq, 1));

      /* The pool may have been closed, and emptied, since the look above:
       * the item is then to be found by whoever empties it after closing,
       * or to go back. The key is put in with an exchange and the closed
       * flag set before a full barrier, so that each side's store comes
       * before the other side's load. */
      (void)retro_atomic_exchange_64(word, key + LF_POOL_BIAS);

      if (retro_atomic_load_acquire_int(&pool->closed)
            && retro_atomic_cas_64(word, key + LF_POOL_BIAS, LF_POOL_OWNED))
      {
         if (slot->item == item)
         {
            lf_pool_empty(pool, i);
            return 0;
         }

         /* Somebody else's by now: this one was taken out */
         retro_atomic_store_release_64(word, key + LF_POOL_BIAS);
      }

      return 1;
   }

   return 0;
}

void *lf_item_pool_take(struct lf_item_pool *pool, uint64_t key,
      uint64_t flags)
{
   size_t i, limit;

   if (key > ~(uint64_t)0 - LF_POOL_BIAS)
      return NULL;

   limit = (size_t)retro_atomic_load_acquire_64(&pool->limit);
   key  += LF_POOL_BIAS;

   for (i = 0; i < limit; i++)
   {
      retro_atomic_64_t *word = &pool->keys[i];

      if ((uint64_t)retro_atomic_load_relaxed_64(word) != key)
         continue;

      if (!retro_atomic_cas_64(word, key, LF_POOL_OWNED))
         continue;

      if ((pool->slots[i].flags & flags) == flags)
         return lf_pool_empty(pool, i);

      retro_atomic_store_release_64(word, key);
   }

   return NULL;
}

void *lf_item_pool_take_any(struct lf_item_pool *pool)
{
   size_t i;

   for (i = 0; i < pool->count; i++)
   {
      uint64_t key = (uint64_t)retro_atomic_load_acquire_64(&pool->keys[i]);

      if (key > LF_POOL_OWNED && retro_atomic_cas_64(&pool->keys[i], key, LF_POOL_OWNED))
         return lf_pool_empty(pool, i);
   }

   return NULL;
}

static int lf_pool_older(const void *a, const void *b)
{
   uint64_t x = *(const uint64_t*)a;
   uint64_t y = *(const uint64_t*)b;
   return (x > y) - (x < y);
}

size_t lf_item_pool_trim(struct lf_item_pool *pool, size_t keep,
      lf_item_pool_drop_t drop, void *ud)
{
   size_t i, n = 0, dropped = 0;
   uint64_t newest_to_go;

   if (!pool->count || !retro_atomic_cas_int(&pool->trimming, 0, 1))
      return 0;

   for (i = 0; i < pool->count; i++)
   {
      if ((uint64_t)retro_atomic_load_acquire_64(&pool->keys[i]) > LF_POOL_OWNED)
         pool->scratch[n++] = (uint64_t)retro_atomic_load_acquire_64(&pool->slots[i].seq);
   }

   if (n > keep)
   {
      qsort(pool->scratch, n, sizeof(*pool->scratch), lf_pool_older);
      newest_to_go = pool->scratch[n - keep - 1];

      for (i = 0; i < pool->count; i++)
      {
         struct lf_item_pool_slot *slot = &pool->slots[i];
         uint64_t key = (uint64_t)retro_atomic_load_acquire_64(&pool->keys[i]);

         if (key <= LF_POOL_OWNED
               || (uint64_t)retro_atomic_load_acquire_64(&slot->seq) > newest_to_go
               || !retro_atomic_cas_64(&pool->keys[i], key, LF_POOL_OWNED))
            continue;

         /* Owned now: what is in it may be a newer item than was seen */
         if ((uint64_t)retro_atomic_load_acquire_64(&slot->seq) > newest_to_go)
         {
            retro_atomic_store_release_64(&pool->keys[i], key);
            continue;
         }

         drop(ud, lf_pool_empty(pool, i));
         dropped++;
      }
   }

   retro_atomic_store_release_int(&pool->trimming, 0);
   return dropped;
}

void lf_item_pool_close(struct lf_item_pool *pool)
{
   (void)retro_atomic_exchange_int(&pool->closed, 1);
}

size_t lf_item_pool_held(const struct lf_item_pool *pool)
{
   size_t limit = (size_t)retro_atomic_load_acquire_64(
         (retro_atomic_64_t*)&pool->limit);
   size_t i, held = 0;

   for (i = 0; i < limit; i++)
      held += (uint64_t)retro_atomic_load_relaxed_64(&pool->keys[i]) > LF_POOL_OWNED;

   return held;
}

uint64_t lf_item_pool_weight(const struct lf_item_pool *pool)
{
   size_t limit = (size_t)retro_atomic_load_acquire_64(
         (retro_atomic_64_t*)&pool->limit);
   size_t i;
   uint64_t weight = 0;

   for (i = 0; i < limit; i++)
   {
      if ((uint64_t)retro_atomic_load_acquire_64(&pool->keys[i]) > LF_POOL_OWNED)
         weight += (uint64_t)retro_atomic_load_relaxed_64(&pool->slots[i].weight);
   }

   return weight;
}
