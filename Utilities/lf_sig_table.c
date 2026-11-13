/* See lf_sig_table.h */

#include "lf_sig_table.h"

#include <stdlib.h>

/* A slot's word: state in the low two bits, the key in the next 34, the
 * times the slot was filled in the rest */
#define LF_SIG_EMPTY 0
#define LF_SIG_OWNED 1 /* being filled or emptied: not to be found */
#define LF_SIG_FULL  2
#define LF_SIG_ASKED 3 /* full, and a sweep is asking about it: found as ever */
#define LF_SIG_FOUND(w, want) (((w) & ((((uint64_t)1 << 36) - 1) & ~(uint64_t)1)) == (want))
#define LF_SIG_STATE(w)   ((w) & 3)
#define LF_SIG_KEY(w)     (((w) >> 2) & LF_SIG_KEY_MAX)
#define LF_SIG_FILLS(w)   ((w) >> 36)
#define LF_SIG_WORD(state, key, fills) \
   ((uint64_t)(state) | (uint64_t)(key) << 2 | (uint64_t)(fills) << 36)

int lf_sig_table_init(struct lf_sig_table *table, size_t count)
{
   table->words = (retro_atomic_64_t*)calloc(count, sizeof(*table->words));
   table->slots = (struct lf_sig_slot*)calloc(count, sizeof(*table->slots));

   if (!table->words || !table->slots)
   {
      lf_sig_table_free(table);
      return 0;
   }

   table->count = count;
   retro_atomic_store_release_64(&table->limit, 0);
   return 1;
}

void lf_sig_table_free(struct lf_sig_table *table)
{
   free(table->words);
   free(table->slots);
   table->words = NULL;
   table->slots = NULL;
   table->count = 0;
}

void *lf_sig_table_find(const struct lf_sig_table *table, uint64_t key,
      const uint64_t *sig)
{
   size_t limit = (size_t)retro_atomic_load_acquire_64(
         (retro_atomic_64_t*)&table->limit);
   uint64_t want = LF_SIG_WORD(LF_SIG_FULL, key, 0);
   size_t i;

   for (i = 0; i < limit; i++)
   {
      struct lf_sig_slot *slot;
      uint64_t word = (uint64_t)retro_atomic_load_relaxed_64(&table->words[i]);
      void *item;
      int n;

      /* State and key at once */
      if (!LF_SIG_FOUND(word, want))
         continue;

      word = (uint64_t)retro_atomic_load_acquire_64(&table->words[i]);
      if (!LF_SIG_FOUND(word, want))
         continue;

      slot = &table->slots[i];

      for (n = 0; n < LF_SIG_WORDS; n++)
      {
         if ((uint64_t)retro_atomic_load_relaxed_64(&slot->sig[n]) != sig[n])
            break;
      }

      if (n != LF_SIG_WORDS)
         continue;

      item = retro_atomic_load_relaxed_ptr(&slot->item);

      /* Filled again since the first look: what was read is of two
       * entries, neither of which need be the one looked for */
      if (((uint64_t)retro_atomic_load_acquire_64(&table->words[i]) | 1) != (word | 1))
         continue;

      return item;
   }

   return NULL;
}

int lf_sig_table_add(struct lf_sig_table *table, uint64_t key,
      const uint64_t *sig, void *item)
{
   size_t i;

   if (key > LF_SIG_KEY_MAX || !item)
      return 0;

   for (i = 0; i < table->count; i++)
   {
      struct lf_sig_slot *slot = &table->slots[i];
      uint64_t word = (uint64_t)retro_atomic_load_relaxed_64(&table->words[i]);
      uint64_t have;
      int n;

      if (LF_SIG_STATE(word) != LF_SIG_EMPTY)
         continue;

      if (!retro_atomic_cas_64(&table->words[i], word,
               LF_SIG_WORD(LF_SIG_OWNED, 0, LF_SIG_FILLS(word))))
         continue;

      /* The limit is above this slot before anything is to be found in
       * it; it only comes down past a slot while that slot is owned by
       * the thread moving it (see the sweep) */
      have = (uint64_t)retro_atomic_load_acquire_64(&table->limit);
      while (have < i + 1 && !retro_atomic_cas_64(&table->limit, have, i + 1))
         have = (uint64_t)retro_atomic_load_acquire_64(&table->limit);

      for (n = 0; n < LF_SIG_WORDS; n++)
         retro_atomic_store_relaxed_64(&slot->sig[n], sig[n]);
      retro_atomic_store_relaxed_ptr(&slot->item, item);

      retro_atomic_store_release_64(&table->words[i],
            LF_SIG_WORD(LF_SIG_FULL, key, LF_SIG_FILLS(word) + 1));
      return 1;
   }

   return 0;
}

size_t lf_sig_table_sweep(struct lf_sig_table *table,
      lf_sig_table_gone_t gone, lf_sig_table_drop_t drop, void *ud)
{
   size_t limit = (size_t)retro_atomic_load_acquire_64(&table->limit);
   size_t went = 0;
   size_t i;

   /* From the top, so that the limit follows the slots emptied down */
   for (i = limit; i-- > 0;)
   {
      uint64_t word, fills;
      void *item;
      int have = 0;

      /* A slot is marked while its item is asked about - still to be
       * found by lookups - and owned while it is emptied. Another thread
       * that has it right now - one adding to it, or one sweeping that is
       * asking its own question - is waited for: its answer is not this
       * sweep's, and an item this sweep was to take out must not be
       * passed over. Lookups and adds never wait. */
      for (;;)
      {
         int state;

         word  = (uint64_t)retro_atomic_load_acquire_64(&table->words[i]);
         state = (int)LF_SIG_STATE(word);

         if (state == LF_SIG_OWNED || state == LF_SIG_ASKED)
            continue;

         /* An empty slot is of interest only at the top, to bring the
          * limit down past it */
         if (state == LF_SIG_EMPTY
               && (uint64_t)retro_atomic_load_relaxed_64(&table->limit) != i + 1)
            break;

         if (retro_atomic_cas_64(&table->words[i], word,
                  state == LF_SIG_EMPTY ? (word | LF_SIG_OWNED) : (word | 1)))
         {
            have = state == LF_SIG_EMPTY ? 1 : 2;
            break;
         }
      }

      if (!have)
         continue;

      fills = LF_SIG_FILLS(word);
      item  = NULL;

      if (have == 2)
      {
         item = retro_atomic_load_relaxed_ptr(&table->slots[i].item);

         if (!gone(ud, item))
         {
            retro_atomic_store_release_64(&table->words[i], word);
            continue;
         }

         /* Not to be found from here on */
         retro_atomic_store_release_64(&table->words[i],
               LF_SIG_WORD(LF_SIG_OWNED, 0, fills));
         retro_atomic_store_relaxed_ptr(&table->slots[i].item, NULL);
         went++;
      }

      if ((uint64_t)retro_atomic_load_relaxed_64(&table->limit) == i + 1)
         retro_atomic_cas_64(&table->limit, i + 1, i);

      retro_atomic_store_release_64(&table->words[i],
            LF_SIG_WORD(LF_SIG_EMPTY, 0, fills));

      if (item && drop)
         drop(ud, item);
   }

   return went;
}

size_t lf_sig_table_held(const struct lf_sig_table *table)
{
   size_t limit = (size_t)retro_atomic_load_acquire_64(
         (retro_atomic_64_t*)&table->limit);
   size_t i, held = 0;

   for (i = 0; i < limit; i++)
      held += LF_SIG_STATE((uint64_t)retro_atomic_load_relaxed_64(&table->words[i])) >= LF_SIG_FULL;

   return held;
}
