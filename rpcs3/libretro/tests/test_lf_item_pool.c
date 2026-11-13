/* The pool of spare things (Utilities/lf_item_pool.c): put in and taken
 * out with no lock.
 *
 * - an item comes out for its key and only when it has the flag bits
 *   asked for; a full or closed pool takes nothing, and says so;
 * - trimming throws away the oldest and keeps the newest;
 * - four threads putting items in and taking them out by key while a
 *   fifth trims: no item is ever handed to two, none is lost - at the end
 *   every item is held by a thread, was trimmed away, or is in the pool -
 *   and the pool's count and weight are those of what is in it, every one
 *   of which a search by key finds (the pool looks only as far up its
 *   slots as it believes there are items);
 * - four threads each putting an item in and taking it straight back out:
 *   it is always found;
 * - the pool is closed and emptied while threads put items in: every item
 *   the pool said it took was handed back out, and the pool is empty.
 *   (Built a second time with ThreadSanitizer.)
 *
 * Usage: test_lf_item_pool */

#include "../../../Utilities/lf_item_pool.h"
#include "test_common.h"

#include <pthread.h>

static void *item_of(size_t n) { return (void*)(n + 1); }
static size_t number_of(void *item) { return (size_t)item - 1; }

static void count_drop(void *ud, void *item)
{
   size_t *seen = (size_t*)ud;
   seen[0]++;
   seen[1] += number_of(item);
}

static void alone(void)
{
   struct lf_item_pool pool;
   size_t seen[2];
   size_t i;

   memset(&pool, 0, sizeof(pool));
   TEST_CHECK(!lf_item_pool_put(&pool, 5, 0, 1, item_of(0)));
   TEST_CHECK(lf_item_pool_take(&pool, 5, 0) == NULL);
   TEST_CHECK(lf_item_pool_take_any(&pool) == NULL);
   TEST_CHECK(lf_item_pool_trim(&pool, 0, count_drop, seen) == 0);
   lf_item_pool_free(&pool);

   TEST_CHECK(lf_item_pool_init(&pool, 8));
   TEST_CHECK(lf_item_pool_take(&pool, 5, 0) == NULL);

   /* Keys 0 and 1 are keys like any other; the two highest are not */
   TEST_CHECK(lf_item_pool_put(&pool, 0, 0, 10, item_of(0)));
   TEST_CHECK(lf_item_pool_put(&pool, 1, 0, 20, item_of(1)));
   TEST_CHECK(!lf_item_pool_put(&pool, ~(uint64_t)0, 0, 1, item_of(9)));
   TEST_CHECK(!lf_item_pool_put(&pool, ~(uint64_t)0 - 1, 0, 1, item_of(9)));
   TEST_CHECK(lf_item_pool_take(&pool, ~(uint64_t)0, 0) == NULL);
   TEST_CHECK(lf_item_pool_held(&pool) == 2);
   TEST_CHECK(lf_item_pool_weight(&pool) == 30);
   TEST_CHECK(lf_item_pool_take(&pool, 2, 0) == NULL);
   TEST_CHECK(lf_item_pool_take(&pool, 1, 0) == item_of(1));
   TEST_CHECK(lf_item_pool_take(&pool, 1, 0) == NULL);
   TEST_CHECK(lf_item_pool_take(&pool, 0, 0) == item_of(0));
   TEST_CHECK(lf_item_pool_held(&pool) == 0);
   TEST_CHECK(lf_item_pool_weight(&pool) == 0);

   /* The flag bits asked for must all be there; the one that lacks them
    * stays */
   TEST_CHECK(lf_item_pool_put(&pool, 7, 0x3, 1, item_of(2)));
   TEST_CHECK(lf_item_pool_put(&pool, 7, 0x6, 1, item_of(3)));
   TEST_CHECK(lf_item_pool_take(&pool, 7, 0x8) == NULL);
   TEST_CHECK(lf_item_pool_take(&pool, 7, 0x4) == item_of(3));
   TEST_CHECK(lf_item_pool_take(&pool, 7, 0x4) == NULL);
   TEST_CHECK(lf_item_pool_take(&pool, 7, 0x1) == item_of(2));

   /* Eight fit; the oldest five are the ones trimmed away */
   for (i = 0; i < 8; i++)
      TEST_CHECK(lf_item_pool_put(&pool, 100 + i, 0, 1, item_of(i)));
   TEST_CHECK(!lf_item_pool_put(&pool, 200, 0, 1, item_of(8)));
   TEST_CHECK(lf_item_pool_take(&pool, 101, 0) == item_of(1));
   TEST_CHECK(lf_item_pool_put(&pool, 200, 0, 1, item_of(8)));
   seen[0] = seen[1] = 0;
   TEST_CHECK(lf_item_pool_trim(&pool, 3, count_drop, seen) == 5);
   TEST_CHECK(seen[0] == 5 && seen[1] == 0 + 2 + 3 + 4 + 5);
   TEST_CHECK(lf_item_pool_held(&pool) == 3);
   TEST_CHECK(lf_item_pool_trim(&pool, 3, count_drop, seen) == 0);
   TEST_CHECK(lf_item_pool_take(&pool, 106, 0) == item_of(6));
   TEST_CHECK(lf_item_pool_take(&pool, 102, 0) == NULL);

   /* How far up the slots are looked at follows what is in them */
   TEST_CHECK((size_t)retro_atomic_load_acquire_64(&pool.limit) == 8);
   TEST_CHECK(lf_item_pool_take(&pool, 200, 0) == item_of(8));
   TEST_CHECK((size_t)retro_atomic_load_acquire_64(&pool.limit) == 8);
   TEST_CHECK(lf_item_pool_take(&pool, 107, 0) == item_of(7));
   TEST_CHECK((size_t)retro_atomic_load_acquire_64(&pool.limit) == 0);
   /* The slot last emptied is the first filled, then the lowest */
   TEST_CHECK(lf_item_pool_put(&pool, 107, 0, 1, item_of(7)));
   TEST_CHECK((size_t)retro_atomic_load_acquire_64(&pool.limit) == 8);
   TEST_CHECK(lf_item_pool_put(&pool, 200, 0, 1, item_of(8)));
   TEST_CHECK((size_t)retro_atomic_load_acquire_64(&pool.limit) == 8);
   TEST_CHECK(lf_item_pool_take(&pool, 107, 0) == item_of(7));
   TEST_CHECK((size_t)retro_atomic_load_acquire_64(&pool.limit) == 1);
   TEST_CHECK(lf_item_pool_put(&pool, 107, 0, 1, item_of(7)));


   lf_item_pool_close(&pool);
   TEST_CHECK(!lf_item_pool_put(&pool, 300, 0, 1, item_of(9)));
   TEST_CHECK(lf_item_pool_take_any(&pool) != NULL);
   TEST_CHECK(lf_item_pool_take_any(&pool) != NULL);
   TEST_CHECK(lf_item_pool_take_any(&pool) == NULL);
   TEST_CHECK(lf_item_pool_held(&pool) == 0);
   TEST_CHECK(lf_item_pool_weight(&pool) == 0);
   lf_item_pool_free(&pool);
}

#define WORKERS 4
#define ITEMS   64     /* each worker starts with this many */
#define ROUNDS  40000
#define TOTAL   (WORKERS * ITEMS)

static struct lf_item_pool shared;
static retro_atomic_int_t in_pool[TOTAL]; /* 1 while the pool has it */
static retro_atomic_int_t bad;
static retro_atomic_int_t stop;
static retro_atomic_int_t trimmed;
static size_t left[WORKERS];

static uint64_t key_of(size_t n) { return n % 8; }

static void went_in(size_t n)
{
   if (!retro_atomic_cas_int(&in_pool[n], 0, 1))
      retro_atomic_fetch_add_int(&bad, 1);
}

static void came_out(size_t n)
{
   if (n >= TOTAL || !retro_atomic_cas_int(&in_pool[n], 1, 0))
      retro_atomic_fetch_add_int(&bad, 1);
}

static void *worker(void *arg)
{
   size_t t = (size_t)arg;
   size_t hold[TOTAL];
   size_t held = 0, i;
   uint32_t x = (uint32_t)t * 2654435761u + 1;

   for (i = 0; i < ITEMS; i++)
      hold[held++] = t * ITEMS + i;

   for (i = 0; i < ROUNDS; i++)
   {
      x = x * 1664525u + 1013904223u;

      if (held && (x >> 16 & 1))
      {
         size_t n = hold[--held];
         went_in(n);
         if (!lf_item_pool_put(&shared, key_of(n), n & 1 ? 0x3 : 0x1, n + 1, item_of(n)))
         {
            came_out(n);
            hold[held++] = n;
         }
      }
      else
      {
         uint64_t flags = x >> 20 & 1 ? 0x2 : 0;
         void *item = lf_item_pool_take(&shared, x >> 24 & 7, flags);
         if (item)
         {
            size_t n = number_of(item);
            came_out(n);
            if (n < TOTAL && (key_of(n) != (x >> 24 & 7) || (flags && !(n & 1))))
               retro_atomic_fetch_add_int(&bad, 1);
            hold[held++] = n;
         }
      }
   }

   left[t] = held;
   return NULL;
}

static void trim_drop(void *ud, void *item)
{
   (void)ud;
   came_out(number_of(item));
   retro_atomic_fetch_add_int(&trimmed, 1);
}

static void *trimmer(void *arg)
{
   (void)arg;
   while (!retro_atomic_load_acquire_int(&stop))
      lf_item_pool_trim(&shared, 24, trim_drop, NULL);
   return NULL;
}

static void together(void)
{
   pthread_t w[WORKERS], tr;
   size_t t, n, with_threads = 0, in = 0;
   uint64_t weight = 0;

   TEST_CHECK(lf_item_pool_init(&shared, 48));
   pthread_create(&tr, NULL, trimmer, NULL);
   for (t = 0; t < WORKERS; t++)
      pthread_create(&w[t], NULL, worker, (void*)t);
   for (t = 0; t < WORKERS; t++)
   {
      pthread_join(w[t], NULL);
      with_threads += left[t];
   }
   retro_atomic_store_release_int(&stop, 1);
   pthread_join(tr, NULL);

   for (n = 0; n < TOTAL; n++)
   {
      if (retro_atomic_load_acquire_int(&in_pool[n]))
      {
         in++;
         weight += n + 1;
      }
   }

   TEST_CHECK(retro_atomic_load_acquire_int(&bad) == 0);
   TEST_CHECK(retro_atomic_load_acquire_int(&trimmed) > 0);
   TEST_CHECK(with_threads + in + (size_t)retro_atomic_load_acquire_int(&trimmed) == TOTAL);
   TEST_CHECK(lf_item_pool_held(&shared) == in);
   TEST_CHECK(lf_item_pool_weight(&shared) == weight);

   /* By key first: what a search cannot see would be left for take_any */
   for (n = 0, t = 0; t < 8; t++)
   {
      while (lf_item_pool_take(&shared, t, 0))
         n++;
   }
   TEST_CHECK(n == in);
   TEST_CHECK(lf_item_pool_take_any(&shared) == NULL);
   TEST_CHECK(retro_atomic_load_acquire_64(&shared.limit) == 0);
   TEST_CHECK(lf_item_pool_held(&shared) == 0);
   TEST_CHECK(lf_item_pool_weight(&shared) == 0);
   lf_item_pool_free(&shared);
}

/* Each thread puts one item in and takes it straight back out, so the top
 * slot in use changes hands all the time: an item just put is always
 * found, however the pool's idea of how far up to look moves meanwhile */
static void *churner(void *arg)
{
   size_t t = (size_t)arg;
   size_t i;
   for (i = 0; i < 300000; i++)
   {
      if (!lf_item_pool_put(&shared, t, 0, 1, item_of(t))
            || lf_item_pool_take(&shared, t, 0) != item_of(t))
         retro_atomic_fetch_add_int(&bad, 1);
   }
   return NULL;
}

static void churning(void)
{
   pthread_t p[WORKERS];
   size_t t;

   TEST_CHECK(lf_item_pool_init(&shared, 16));
   retro_atomic_store_release_int(&bad, 0);
   for (t = 0; t < WORKERS; t++)
      pthread_create(&p[t], NULL, churner, (void*)t);
   for (t = 0; t < WORKERS; t++)
      pthread_join(p[t], NULL);
   TEST_CHECK(retro_atomic_load_acquire_int(&bad) == 0);
   TEST_CHECK(lf_item_pool_held(&shared) == 0);
   TEST_CHECK(retro_atomic_load_acquire_64(&shared.limit) == 0);
   lf_item_pool_free(&shared);
}

/* Closing while items go in */
static retro_atomic_int_t taken_in;

static void *putter(void *arg)
{
   size_t t = (size_t)arg;
   size_t i;
   for (i = 0; i < 200000; i++)
   {
      if (lf_item_pool_put(&shared, i & 3, 0, 1, item_of(t)))
         retro_atomic_fetch_add_int(&taken_in, 1);
      else if (retro_atomic_load_acquire_int(&stop) == 2)
         break;
   }
   return NULL;
}

static void closing(void)
{
   pthread_t p[WORKERS];
   size_t t;
   int round, out;

   for (round = 0; round < 20; round++)
   {
      TEST_CHECK(lf_item_pool_init(&shared, 4096));
      retro_atomic_store_release_int(&taken_in, 0);
      retro_atomic_store_release_int(&stop, 0);
      for (t = 0; t < WORKERS; t++)
         pthread_create(&p[t], NULL, putter, (void*)t);

      while (retro_atomic_load_acquire_int(&taken_in) < 100 * (round + 1))
         ;
      lf_item_pool_close(&shared);
      for (out = 0; lf_item_pool_take_any(&shared); out++)
         ;
      retro_atomic_store_release_int(&stop, 2);
      for (t = 0; t < WORKERS; t++)
         pthread_join(p[t], NULL);

      TEST_CHECK(out == retro_atomic_load_acquire_int(&taken_in));
      TEST_CHECK(lf_item_pool_held(&shared) == 0);
      TEST_CHECK(lf_item_pool_take_any(&shared) == NULL);
      lf_item_pool_free(&shared);
   }
}

int main(void)
{
   alone();
   together();
   churning();
   closing();
   printf("test_lf_item_pool: %s\n", test_failures ? "FAIL" : "ok");
   return test_failures ? 1 : 0;
}
