/* Hazard cells (Utilities/lf_hazard.c).
 *
 * - a thread's record holds as many objects as it has cells and no more;
 *   an object is reported held exactly while a cell holds it; records
 *   run out at LF_HAZARD_THREADS and come back when released;
 * - the use they are for: four readers look at whatever object a shared
 *   slot points at - load, hold, load again, use, drop - while a fifth
 *   thread swaps in a new object 20000 times, waits until the old one
 *   is no longer held and then poisons and recycles it. No reader ever
 *   sees a poisoned object. (Built a second time with ThreadSanitizer.)
 *
 * Usage: test_lf_hazard */

#include "../../../Utilities/lf_hazard.h"
#include "test_common.h"

#include <pthread.h>
#include <sched.h>
#include <retro_atomic.h>

static void alone(void)
{
   static int things[LF_HAZARD_CELLS + 1];
   int claimed[LF_HAZARD_THREADS];
   int r, i, cells[LF_HAZARD_CELLS];

   r = lf_hazard_claim();
   TEST_CHECK(r >= 0);
   TEST_CHECK(!lf_hazard_any(&things[0]));

   for (i = 0; i < LF_HAZARD_CELLS; i++)
   {
      cells[i] = lf_hazard_hold(r, &things[i]);
      TEST_CHECK(cells[i] >= 0);
      TEST_CHECK(lf_hazard_any(&things[i]));
   }

   /* No cell left for a fifth */
   TEST_CHECK(lf_hazard_hold(r, &things[LF_HAZARD_CELLS]) == -1);
   TEST_CHECK(!lf_hazard_any(&things[LF_HAZARD_CELLS]));

   lf_hazard_drop(r, cells[1]);
   TEST_CHECK(!lf_hazard_any(&things[1]));
   TEST_CHECK(lf_hazard_any(&things[0]) && lf_hazard_any(&things[2]));
   TEST_CHECK(lf_hazard_hold(r, &things[LF_HAZARD_CELLS]) == cells[1]);
   TEST_CHECK(lf_hazard_any(&things[LF_HAZARD_CELLS]));

   for (i = 0; i < LF_HAZARD_CELLS; i++)
      lf_hazard_drop(r, i);
   TEST_CHECK(!lf_hazard_any(&things[0]) && !lf_hazard_any(&things[LF_HAZARD_CELLS]));

   /* All the records, then none; one given back is the one got next */
   claimed[0] = r;
   for (i = 1; i < LF_HAZARD_THREADS; i++)
   {
      claimed[i] = lf_hazard_claim();
      TEST_CHECK(claimed[i] >= 0);
   }
   TEST_CHECK(lf_hazard_claim() == -1);
   lf_hazard_release(claimed[100]);
   TEST_CHECK(lf_hazard_claim() == claimed[100]);

   /* A cell in the last record is seen */
   i = lf_hazard_hold(claimed[LF_HAZARD_THREADS - 1], &things[0]);
   TEST_CHECK(lf_hazard_any(&things[0]));
   lf_hazard_drop(claimed[LF_HAZARD_THREADS - 1], i);

   for (i = 0; i < LF_HAZARD_THREADS; i++)
      lf_hazard_release(claimed[i]);
}

#define READERS 4
#define SWAPS   20000
#define POISON  0x5A5A5A5A

struct object
{
   retro_atomic_int_t value; /* POISON once taken away */
};

static struct object pool[2];
static retro_atomic_ptr_t slot;
static retro_atomic_int_t stop;
static retro_atomic_int_t bad;
static retro_atomic_int_t looked;

static void *reader(void *arg)
{
   int r = lf_hazard_claim();
   (void)arg;

   if (r < 0)
   {
      retro_atomic_fetch_add_int(&bad, 1);
      return NULL;
   }

   while (!retro_atomic_load_acquire_int(&stop))
   {
      struct object *o = (struct object*)retro_atomic_load_acquire_ptr(&slot);
      int cell;

      if (!o)
         continue;

      cell = lf_hazard_hold(r, o);

      if (retro_atomic_load_acquire_ptr(&slot) == o)
      {
         /* Held, and it was still in the slot after that: not to be
          * poisoned until dropped */
         int i;
         for (i = 0; i < 8; i++)
         {
            if (retro_atomic_load_acquire_int(&o->value) == POISON)
               retro_atomic_fetch_add_int(&bad, 1);
         }
         retro_atomic_fetch_add_int(&looked, 1);
      }

      lf_hazard_drop(r, cell);

      /* Off the processor while holding nothing, so that it is seldom
       * put off it while holding: the swapper waits out every holder,
       * and with more threads than processors that would be a time slice
       * per swap */
      sched_yield();
   }

   lf_hazard_release(r);
   return NULL;
}

static void together(void)
{
   pthread_t t[READERS];
   int i, n;

   retro_atomic_store_release_int(&pool[0].value, 1);
   retro_atomic_store_release_int(&pool[1].value, POISON);
   retro_atomic_store_release_ptr(&slot, &pool[0]);

   for (i = 0; i < READERS; i++)
      pthread_create(&t[i], NULL, reader, NULL);

   for (n = 0; n < SWAPS; n++)
   {
      struct object *fresh = &pool[(n + 1) & 1];
      struct object *old;

      /* Made whole, put in, and the old one taken away once nobody
       * is looking at it */
      retro_atomic_store_release_int(&fresh->value, n + 2);
      old = (struct object*)retro_atomic_exchange_ptr(&slot, fresh);

      /* (A reader may have been put off the processor while it holds
       * the old one: let it run rather than spin a time slice away) */
      while (lf_hazard_any(old))
         sched_yield();

      retro_atomic_store_release_int(&old->value, POISON);
   }

   retro_atomic_store_release_int(&stop, 1);
   for (i = 0; i < READERS; i++)
      pthread_join(t[i], NULL);

   TEST_CHECK(retro_atomic_load_acquire_int(&bad) == 0);
   TEST_CHECK(retro_atomic_load_acquire_int(&looked) > 0);
   TEST_CHECK(!lf_hazard_any(&pool[0]) && !lf_hazard_any(&pool[1]));
}

int main(void)
{
   alone();
   together();
   printf("test_lf_hazard: %s\n", test_failures ? "FAIL" : "ok");
   return test_failures ? 1 : 0;
}
