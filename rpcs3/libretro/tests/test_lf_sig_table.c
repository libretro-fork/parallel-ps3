/* The table of things known by key and signature
 * (Utilities/lf_sig_table.c): looked up, added to and swept with no lock.
 *
 * - an item is found by its key and all eight words of its signature and
 *   by nothing less; a full table takes nothing more; a sweep takes out
 *   what it is told to and leaves the rest, and the table looks only as
 *   far up its slots as there are entries;
 * - four threads each add items of their own, look them up and sweep
 *   them out again, over and over, while two more look up everybody's:
 *   an item is always found between its being added and its being swept
 *   and never after, and no lookup ever comes back with a different item
 *   than the one whose key and signature it gave - the slots are reused
 *   for items with the same key all the time, so a lookup that put
 *   together one item's signature and another's pointer would show;
 * - one slot with two things of the same key taking turns in it, and
 *   lookups for both: each gets the thing it asked for, or nothing.
 *   (Built a second time with ThreadSanitizer.)
 *
 * Usage: test_lf_sig_table */

#include "../../../Utilities/lf_sig_table.h"
#include "test_common.h"

#include <pthread.h>
#include <sched.h>

struct thing
{
   uint64_t key;
   uint64_t sig[LF_SIG_WORDS];
   int go;
};

static void make(struct thing *t, size_t n)
{
   int k;
   t->key = n % 4;
   for (k = 0; k < LF_SIG_WORDS; k++)
      t->sig[k] = (uint64_t)n * 0x9E3779B97F4A7C15ull + (uint64_t)k;
   t->go = 0;
}

static int told_to_go(void *ud, void *item)
{
   (void)ud;
   return ((struct thing*)item)->go;
}

static void count_dropped(void *ud, void *item)
{
   (void)item;
   ++*(size_t*)ud;
}

static void alone(void)
{
   size_t dropped = 0;
   struct lf_sig_table table;
   static struct thing things[40];
   uint64_t sig[LF_SIG_WORDS];
   size_t i;
   int k, wrong = 0;

   for (i = 0; i < 40; i++)
      make(&things[i], i);

   memset(&table, 0, sizeof(table));
   TEST_CHECK(lf_sig_table_find(&table, 1, things[1].sig) == NULL);
   TEST_CHECK(!lf_sig_table_add(&table, 1, things[1].sig, &things[1]));
   TEST_CHECK(lf_sig_table_sweep(&table, told_to_go, count_dropped, &dropped) == 0);
   lf_sig_table_free(&table);

   TEST_CHECK(lf_sig_table_init(&table, 32));
   TEST_CHECK(lf_sig_table_find(&table, 1, things[1].sig) == NULL);
   TEST_CHECK(lf_sig_table_add(&table, 1, things[1].sig, &things[1]));
   TEST_CHECK(lf_sig_table_find(&table, 1, things[1].sig) == &things[1]);

   /* Another key, or any one word of the signature off: not it */
   TEST_CHECK(lf_sig_table_find(&table, 2, things[1].sig) == NULL);
   for (k = 0; k < LF_SIG_WORDS; k++)
   {
      memcpy(sig, things[1].sig, sizeof(sig));
      sig[k] ^= 1;
      wrong += lf_sig_table_find(&table, 1, sig) != NULL;
   }
   TEST_CHECK(wrong == 0);

   /* Key 0 and the highest key are keys; above that is not */
   TEST_CHECK(lf_sig_table_add(&table, 0, things[0].sig, &things[0]));
   TEST_CHECK(lf_sig_table_find(&table, 0, things[0].sig) == &things[0]);
   TEST_CHECK(lf_sig_table_add(&table, LF_SIG_KEY_MAX, things[3].sig, &things[3]));
   TEST_CHECK(lf_sig_table_find(&table, LF_SIG_KEY_MAX, things[3].sig) == &things[3]);
   TEST_CHECK(lf_sig_table_find(&table, LF_SIG_KEY_MAX - 1, things[3].sig) == NULL);
   TEST_CHECK(!lf_sig_table_add(&table, LF_SIG_KEY_MAX + 1, things[3].sig, &things[3]));
   TEST_CHECK(!lf_sig_table_add(&table, 1, things[1].sig, NULL));

   /* 32 fit */
   for (i = 4; i < 33; i++)
      TEST_CHECK(lf_sig_table_add(&table, things[i].key, things[i].sig, &things[i]));
   TEST_CHECK(!lf_sig_table_add(&table, things[33].key, things[33].sig, &things[33]));
   TEST_CHECK(lf_sig_table_held(&table) == 32);
   TEST_CHECK((size_t)retro_atomic_load_acquire_64(&table.limit) == 32);

   /* The odd ones go; the even ones stay, and are found */
   for (i = 0; i < 40; i++)
      things[i].go = (int)(i & 1);
   TEST_CHECK(lf_sig_table_sweep(&table, told_to_go, count_dropped, &dropped) == 16);
   TEST_CHECK(lf_sig_table_sweep(&table, told_to_go, count_dropped, &dropped) == 0);
   wrong = 0;
   for (i = 4; i < 33; i++)
      wrong += lf_sig_table_find(&table, things[i].key, things[i].sig)
         != ((i & 1) ? NULL : (void*)&things[i]);
   TEST_CHECK(wrong == 0);
   TEST_CHECK(lf_sig_table_held(&table) == 16);

   /* A freed slot is used again, and what was in it is not found there */
   TEST_CHECK(lf_sig_table_add(&table, things[35].key, things[35].sig, &things[35]));
   TEST_CHECK(lf_sig_table_find(&table, things[35].key, things[35].sig) == &things[35]);
   TEST_CHECK(lf_sig_table_find(&table, things[1].key, things[1].sig) == NULL);

   /* All go: the table looks at no slot */
   for (i = 0; i < 40; i++)
      things[i].go = 1;
   TEST_CHECK(lf_sig_table_sweep(&table, told_to_go, count_dropped, &dropped) == 17);
   TEST_CHECK(lf_sig_table_held(&table) == 0);
   TEST_CHECK(retro_atomic_load_acquire_64(&table.limit) == 0);
   TEST_CHECK(lf_sig_table_find(&table, 0, things[0].sig) == NULL);
   TEST_CHECK(dropped == 33);
   lf_sig_table_free(&table);
}

#define OWNERS  4
#define LOOKERS 2
#define EACH    48
#define ROUNDS  15000
#define THINGS  (OWNERS * EACH)

static struct lf_sig_table shared;
static struct thing all[THINGS];
static retro_atomic_int_t bad;
static retro_atomic_int_t stop;
static retro_atomic_int_t found_some;

struct mine
{
   struct thing *going;
};

static int is_going(void *ud, void *item)
{
   return ((struct mine*)ud)->going == item;
}

/* Out of the table by the time it is handed over */
static void was_dropped(void *ud, void *item)
{
   struct thing *it = (struct thing*)item;
   if (it != ((struct mine*)ud)->going
         || lf_sig_table_find(&shared, it->key, it->sig) != NULL)
      retro_atomic_fetch_add_int(&bad, 1);
}

static void *owner(void *arg)
{
   size_t t = (size_t)arg;
   struct mine m;
   uint32_t x = (uint32_t)t * 2654435761u + 1;
   size_t i;

   for (i = 0; i < ROUNDS; i++)
   {
      struct thing *it;
      x  = x * 1664525u + 1013904223u;
      it = &all[t * EACH + (x >> 16) % EACH];

      if (lf_sig_table_find(&shared, it->key, it->sig) != NULL
            || !lf_sig_table_add(&shared, it->key, it->sig, it)
            || lf_sig_table_find(&shared, it->key, it->sig) != it)
         retro_atomic_fetch_add_int(&bad, 1);

      /* Until a lookup has found a thing in the table, the thing is left
       * there for a moment: on a machine busy with something else the
       * lookups otherwise may never come by while one is in, and the run
       * has then tested nothing (it failed that check under load) */
      if (!retro_atomic_load_acquire_int(&found_some))
         sched_yield();

      m.going = it;
      if (lf_sig_table_sweep(&shared, is_going, was_dropped, &m) != 1
            || lf_sig_table_find(&shared, it->key, it->sig) != NULL)
         retro_atomic_fetch_add_int(&bad, 1);
   }

   return NULL;
}

static void *looker(void *arg)
{
   uint32_t x = (uint32_t)(size_t)arg * 40503u + 7;

   while (!retro_atomic_load_acquire_int(&stop))
   {
      struct thing *it, *got;
      x   = x * 1664525u + 1013904223u;
      it  = &all[(x >> 12) % THINGS];
      got = (struct thing*)lf_sig_table_find(&shared, it->key, it->sig);

      if (got && got != it)
         retro_atomic_fetch_add_int(&bad, 1);
      if (got)
         retro_atomic_store_release_int(&found_some, 1);
   }

   return NULL;
}

static void together(void)
{
   pthread_t o[OWNERS], l[LOOKERS];
   size_t t;

   for (t = 0; t < THINGS; t++)
      make(&all[t], t);

   /* Fewer slots than there are things, so they are used over and over */
   TEST_CHECK(lf_sig_table_init(&shared, 8));
   for (t = 0; t < LOOKERS; t++)
      pthread_create(&l[t], NULL, looker, (void*)t);
   for (t = 0; t < OWNERS; t++)
      pthread_create(&o[t], NULL, owner, (void*)t);
   for (t = 0; t < OWNERS; t++)
      pthread_join(o[t], NULL);
   retro_atomic_store_release_int(&stop, 1);
   for (t = 0; t < LOOKERS; t++)
      pthread_join(l[t], NULL);

   TEST_CHECK(retro_atomic_load_acquire_int(&bad) == 0);
   TEST_CHECK(retro_atomic_load_acquire_int(&found_some) == 1);
   TEST_CHECK(lf_sig_table_held(&shared) == 0);
   TEST_CHECK(retro_atomic_load_acquire_64(&shared.limit) == 0);
   lf_sig_table_free(&shared);
}

/* ThreadSanitizer makes every swap some twenty times dearer */
#ifdef __SANITIZE_THREAD__
#define SWAPS 100000
#else
#define SWAPS 1500000
#endif

/* One slot, two things with the same key taking turns in it as fast as
 * one thread can swap them, and lookups for both all the while: a lookup
 * comes back with the thing it asked for or with nothing */
static int always(void *ud, void *item)
{
   (void)ud;
   (void)item;
   return 1;
}

static void *swapper(void *arg)
{
   size_t i;
   (void)arg;
   for (i = 0; i < SWAPS; i++)
   {
      struct thing *it = &all[i & 1];
      if (!lf_sig_table_add(&shared, 9, it->sig, it)
            || lf_sig_table_sweep(&shared, always, NULL, NULL) != 1)
         retro_atomic_fetch_add_int(&bad, 1);
   }
   retro_atomic_store_release_int(&stop, 1);
   return NULL;
}

static void *asker(void *arg)
{
   size_t n = (size_t)arg & 1;
   size_t hits = 0;

   while (!retro_atomic_load_acquire_int(&stop))
   {
      void *got = lf_sig_table_find(&shared, 9, all[n].sig);
      if (got && got != &all[n])
         retro_atomic_fetch_add_int(&bad, 1);
      hits += got != NULL;
      n ^= 1;
   }

   if (hits)
      retro_atomic_store_release_int(&found_some, 1);
   return NULL;
}

static void taking_turns(void)
{
   pthread_t s, a[3];
   size_t t;

   make(&all[0], 0);
   make(&all[1], 1);
   retro_atomic_store_release_int(&stop, 0);
   retro_atomic_store_release_int(&bad, 0);
   retro_atomic_store_release_int(&found_some, 0);

   TEST_CHECK(lf_sig_table_init(&shared, 1));
   for (t = 0; t < 3; t++)
      pthread_create(&a[t], NULL, asker, (void*)t);
   pthread_create(&s, NULL, swapper, NULL);
   pthread_join(s, NULL);
   for (t = 0; t < 3; t++)
      pthread_join(a[t], NULL);

   TEST_CHECK(retro_atomic_load_acquire_int(&bad) == 0);
   TEST_CHECK(retro_atomic_load_acquire_int(&found_some) == 1);
   lf_sig_table_free(&shared);
}

int main(void)
{
   alone();
   together();
   taking_turns();
   printf("test_lf_sig_table: %s\n", test_failures ? "FAIL" : "ok");
   return test_failures ? 1 : 0;
}
