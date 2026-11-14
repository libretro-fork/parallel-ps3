/* The slots of the threads suspend_all pauses (Emu/CPU/cpu_slots.c): 128
 * bits taken and given back with a 128-bit compare-and-swap.
 *
 * - Alone: slots come lowest first, across the two halves of the word; a
 *   full word gives -1; a slot given back is the next one taken.
 * - Four threads taking and giving back at once: no slot is ever held by
 *   two of them, and at the end none is taken. */

#include "../../Emu/CPU/cpu_slots.h"
#include "test_common.h"

#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <unistd.h>
#include <retro_atomic.h>

static retro_atomic_128_t slots;

static int taken_count(void)
{
   retro_u128_t t = cpu_slots_taken(&slots);
   return __builtin_popcountll(t.lo) + __builtin_popcountll(t.hi);
}

static void alone(void)
{
   int i;
   retro_u128_t t;

   for (i = 0; i < CPU_SLOTS; i++)
      TEST_CHECK(cpu_slots_take(&slots) == i);
   TEST_CHECK(cpu_slots_take(&slots) == -1);
   t = cpu_slots_taken(&slots);
   TEST_CHECK(t.lo == ~(uint64_t)0 && t.hi == ~(uint64_t)0);

   /* Given back: taken again first, in either half */
   cpu_slots_give(&slots, 70);
   cpu_slots_give(&slots, 3);
   TEST_CHECK(cpu_slots_take(&slots) == 3);
   TEST_CHECK(cpu_slots_take(&slots) == 70);
   TEST_CHECK(cpu_slots_take(&slots) == -1);

   for (i = 0; i < CPU_SLOTS; i++)
      cpu_slots_give(&slots, (unsigned)i);
   TEST_CHECK(taken_count() == 0);

   /* The low half full, the high half not */
   for (i = 0; i < 64; i++)
      TEST_CHECK(cpu_slots_take(&slots) == i);
   TEST_CHECK(cpu_slots_take(&slots) == 64);
   for (i = 0; i < 65; i++)
      cpu_slots_give(&slots, (unsigned)i);
   TEST_CHECK(taken_count() == 0);
}

#define THREADS 4
#define ROUNDS  100000

static retro_atomic_int_t holder[CPU_SLOTS];
static retro_atomic_int_t go, twice, none_free;

/* Takes eight slots one after another, then gives them all back: the takes
 * of the threads run into each other as often as they can */
static void *taker(void *arg)
{
   const int me = (int)(size_t)arg + 1;
   int held[8];
   int i, k;

   while (!retro_atomic_load_acquire_int(&go))
      sched_yield();

   for (i = 0; i < ROUNDS; i++)
   {
      for (k = 0; k < 8; k++)
      {
         held[k] = cpu_slots_take(&slots);
         if (held[k] < 0)
         {
            retro_atomic_fetch_add_int(&none_free, 1);
            continue;
         }
         /* Nobody else may hold it */
         if (!retro_atomic_cas_int(&holder[held[k]], 0, me))
            retro_atomic_fetch_add_int(&twice, 1);
      }

      for (k = 0; k < 8; k++)
      {
         if (held[k] < 0)
            continue;
         if (!retro_atomic_cas_int(&holder[held[k]], me, 0))
            retro_atomic_fetch_add_int(&twice, 1);
         cpu_slots_give(&slots, (unsigned)held[k]);
      }
   }

   return NULL;
}

static void together(void)
{
   pthread_t t[THREADS];
   size_t i;

   for (i = 0; i < THREADS; i++)
      pthread_create(&t[i], NULL, taker, (void*)i);
   retro_atomic_store_release_int(&go, 1);
   for (i = 0; i < THREADS; i++)
      pthread_join(t[i], NULL);

   TEST_CHECK(retro_atomic_load_acquire_int(&twice) == 0);
   /* At most 32 are held at once: there is always one free */
   TEST_CHECK(retro_atomic_load_acquire_int(&none_free) == 0);
   TEST_CHECK(taken_count() == 0);
}

int main(void)
{
   alarm(300);
   alone();
   together();
   printf("test_cpu_slots: %s\n", test_failures ? "FAIL" : "ok");
   return test_failures ? 1 : 0;
}
