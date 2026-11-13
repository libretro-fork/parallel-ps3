/* A table of things each known by a key and a signature of eight words,
 * looked up, added and removed by any number of threads with no lock.
 *
 * For a cache whose lookup must not touch the cached thing itself -
 * because another thread may be throwing it away - and so compares what
 * it is looking for against a copy of the thing's identity kept in the
 * table.
 *
 * A fixed number of slots. A slot's word holds its state (empty, owned by
 * one thread, full), the key, and a count of how many times the slot has
 * been filled (a fourth state marks a full slot a sweep is asking about,
 * which lookups treat as full); the words are an array of their own, so a lookup is one
 * plain load per slot over about as many slots as there are entries, and
 * writes nothing at all. A lookup that finds the key reads the signature
 * and the item and then the word again: the same word means the slot was
 * not refilled meanwhile and what was read belongs together.
 *
 * What a lookup returns is the caller's to keep valid: the table does not
 * stop another thread removing it. */

#ifndef LF_SIG_TABLE_H
#define LF_SIG_TABLE_H

#include <stddef.h>
#include <stdint.h>

#include <retro_atomic.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LF_SIG_WORDS   8
#define LF_SIG_KEY_MAX (((uint64_t)1 << 34) - 1)

struct lf_sig_slot
{
   retro_atomic_64_t sig[LF_SIG_WORDS];
   retro_atomic_ptr_t item;
};

struct lf_sig_table
{
   retro_atomic_64_t *words;   /* state, key, times filled */
   struct lf_sig_slot *slots;
   size_t count;
   retro_atomic_64_t limit;    /* no slot from here up has anything in it */
};

/* Nonzero: the item goes */
typedef int (*lf_sig_table_gone_t)(void *ud, void *item);

/* The item is out of the table, and the callback's to dispose of */
typedef void (*lf_sig_table_drop_t)(void *ud, void *item);

/* Room for count entries. 0 when the memory is not to be had. */
int lf_sig_table_init(struct lf_sig_table *table, size_t count);

/* Not while anything uses the table, and after it is emptied. Safe on a
 * zeroed table. */
void lf_sig_table_free(struct lf_sig_table *table);

/* An item with this key and signature, or NULL */
void *lf_sig_table_find(const struct lf_sig_table *table, uint64_t key,
      const uint64_t *sig);

/* Adds the item (not NULL), also when one with the same key and signature
 * is there. 0 when the table is full or the key above LF_SIG_KEY_MAX. */
int lf_sig_table_add(struct lf_sig_table *table, uint64_t key,
      const uint64_t *sig, void *item);

/* Asks gone() about every item and takes out those it says are to go,
 * handing each to drop() (which may be NULL) once it can no longer be
 * found. Returns how many went. An item is found by lookups as ever
 * while it is asked about. Two sweeps at once wait for each other slot by
 * slot, for as long as the other's gone() takes; nothing else waits. */
size_t lf_sig_table_sweep(struct lf_sig_table *table,
      lf_sig_table_gone_t gone, lf_sig_table_drop_t drop, void *ud);

/* Entries in the table, about now */
size_t lf_sig_table_held(const struct lf_sig_table *table);

#ifdef __cplusplus
}
#endif

#endif
