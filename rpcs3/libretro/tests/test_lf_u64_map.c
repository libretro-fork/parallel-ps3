/* The add-only map (Utilities/lf_u64_map.c): 64-bit key to 64-bit value
 * with no lock.
 *
 * - a key that was never added has no value; one that was has the value
 *   it was first given, also when it is added again with another;
 * - the map takes as many keys as it has slots and no more;
 * - four threads adding keys - a quarter of them the same keys, each with
 *   a value of its own - while they and two more look keys up: a key's
 *   value never changes once it has one, every adder is told the value
 *   that stayed, and at the end every key is there. (Built a second time
 *   with ThreadSanitizer.)
 *
 * Usage: test_lf_u64_map */

#include "../../../Utilities/lf_u64_map.h"
#include "test_common.h"

#include <pthread.h>

static void add_up(void *ud, uint64_t key, uint64_t value)
{
   uint64_t *sums = (uint64_t*)ud;
   (void)key;
   sums[0] += 1;
   sums[1] += value;
}

static void alone(void)
{
   struct lf_u64_map map;
   uint64_t i;
   int full = 0, wrong = 0;

   memset(&map, 0, sizeof(map));
   TEST_CHECK(lf_u64_map_get(&map, 5) == 0);
   TEST_CHECK(lf_u64_map_add(&map, 5, 6) == 0);
   lf_u64_map_free(&map);

   TEST_CHECK(lf_u64_map_init(&map, 256));
   TEST_CHECK(lf_u64_map_get(&map, 5) == 0);
   TEST_CHECK(lf_u64_map_add(&map, 5, 600) == 600);
   TEST_CHECK(lf_u64_map_get(&map, 5) == 600);
   TEST_CHECK(lf_u64_map_add(&map, 5, 700) == 600);
   TEST_CHECK(lf_u64_map_get(&map, 5) == 600);
   TEST_CHECK(lf_u64_map_add(&map, ~(uint64_t)0, 1) == 1);
   TEST_CHECK(lf_u64_map_get(&map, ~(uint64_t)0) == 1);
   TEST_CHECK(lf_u64_map_get(&map, 6) == 0);

   for (i = 0; i < 256; i++)
      full += !lf_u64_map_add(&map, 0x1000 + i * 0x10001, i + 1);
   TEST_CHECK(full == 2);
   for (i = 0; i < 254; i++)
      wrong += lf_u64_map_get(&map, 0x1000 + i * 0x10001) != i + 1;
   TEST_CHECK(wrong == 0);
   TEST_CHECK(lf_u64_map_get(&map, 5) == 600);

   /* Every entry, once */
   {
      uint64_t sums[2];
      sums[0] = sums[1] = 0;
      lf_u64_map_each(&map, add_up, sums);
      TEST_CHECK(sums[0] == 256);
      TEST_CHECK(sums[1] == 600 + 1 + (uint64_t)254 * 255 / 2);
   }
   lf_u64_map_free(&map);
}

#define ADDERS 4
#define KEYS   20000

static struct lf_u64_map shared;
static retro_atomic_int_t bad;

/* Key k is thread (k % ADDERS)'s own, but every fourth of those is added
 * by all */
static uint64_t key_of(uint64_t k) { return (k + 1) * 0x9E3779B1ull; }

static void *adder(void *arg)
{
   uint64_t t = (uint64_t)(size_t)arg;
   uint64_t k;
   for (k = 0; k < KEYS; k++)
   {
      int everyones = (k / ADDERS) % 4 == 0;
      if (everyones || k % ADDERS == t)
      {
         uint64_t mine = (t + 1) << 32 | k;
         uint64_t got  = lf_u64_map_add(&shared, key_of(k), mine);
         uint64_t seen = lf_u64_map_get(&shared, key_of(k));
         /* What stayed is somebody's value for this key, and the same
          * every time it is looked at */
         if ((got & 0xFFFFFFFFu) != k || seen != got
               || (!everyones && got != mine))
            retro_atomic_fetch_add_int(&bad, 1);
      }
   }
   return NULL;
}

static void *looker(void *arg)
{
   static uint64_t first[2][KEYS];
   uint64_t *mine = first[(size_t)arg];
   int round;
   uint64_t k;
   for (round = 0; round < 20; round++)
   {
      for (k = 0; k < KEYS; k++)
      {
         uint64_t v = lf_u64_map_get(&shared, key_of(k));
         if (v && (v & 0xFFFFFFFFu) != k)
            retro_atomic_fetch_add_int(&bad, 1);
         if (mine[k] && v != mine[k])
            retro_atomic_fetch_add_int(&bad, 1);
         if (v)
            mine[k] = v;
      }
   }
   return NULL;
}

static void together(void)
{
   pthread_t a[ADDERS], l[2];
   size_t t;
   uint64_t k;
   int missing = 0;

   TEST_CHECK(lf_u64_map_init(&shared, 65536));
   for (t = 0; t < ADDERS; t++)
      pthread_create(&a[t], NULL, adder, (void*)t);
   for (t = 0; t < 2; t++)
      pthread_create(&l[t], NULL, looker, (void*)t);
   for (t = 0; t < ADDERS; t++)
      pthread_join(a[t], NULL);
   for (t = 0; t < 2; t++)
      pthread_join(l[t], NULL);

   TEST_CHECK(retro_atomic_load_acquire_int(&bad) == 0);
   for (k = 0; k < KEYS; k++)
      missing += !lf_u64_map_get(&shared, key_of(k));
   TEST_CHECK(missing == 0);
   lf_u64_map_free(&shared);
}

int main(void)
{
   alone();
   together();
   printf("test_lf_u64_map: %s\n", test_failures ? "FAIL" : "ok");
   return test_failures ? 1 : 0;
}
