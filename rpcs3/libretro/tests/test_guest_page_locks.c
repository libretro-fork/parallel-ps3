/* The table of pages the texture cache has locked and the notes guest
 * threads leave for it (Emu/RSX/Common/guest_page_locks.c).
 *
 * - a page counts the sections that lock it read-only and no-access, by
 *   range; a write fault is taken off the cache's hands only on a page
 *   that is locked read-only and by nothing no-access - not on a page
 *   nothing has locked, which is somebody else's fault to handle, and not
 *   where something has to be written back first;
 * - a note is the faulting page, taken once; with no room for a note the
 *   fault is declined, and nothing is left behind;
 * - four threads leave notes for pages of their own while a fifth takes
 *   them and others lock and unlock ranges over the same pages: every
 *   note left is taken exactly once, and the counts end where they began. (Built a
 *   second time with ThreadSanitizer.)
 *
 * Usage: test_guest_page_locks */

#include "../../Emu/RSX/Common/guest_page_locks.h"
#include "test_common.h"

#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <retro_atomic.h>

/* A guest write fault, start to finish: 1 when a note was left */
static int note_write(uint32_t address)
{
   if (rsx_page_locks_begin_write(address) != 1)
      return 0;
   return rsx_page_locks_end_write(address);
}

static void counts(uint32_t address, uint32_t ro, uint32_t na)
{
   uint32_t have_ro, have_na;
   rsx_page_locks_get(address, &have_ro, &have_na);
   TEST_CHECK(have_ro == ro);
   TEST_CHECK(have_na == na);
}

static void alone(void)
{
   uint32_t page = 0;
   uint32_t i;
   int declined = 0;

   rsx_page_locks_reset();
   TEST_CHECK(!rsx_page_locks_pending());
   TEST_CHECK(!rsx_page_locks_take(&page));

   /* Nothing has the page locked: not the cache's fault */
   TEST_CHECK(!note_write(0x30001234));
   TEST_CHECK(!rsx_page_locks_pending());

   /* Ranges are by address, both ends in: 0x30001000..0x30003FFF is three
    * pages, and the pages either side are not touched */
   rsx_page_locks_add(0x30001000, 0x30003FFF, 0);
   counts(0x30000FFF, 0, 0);
   counts(0x30001000, 1, 0);
   counts(0x30002ABC, 1, 0);
   counts(0x30003FFF, 1, 0);
   counts(0x30004000, 0, 0);

   /* Read-only only: taken, and the note is the page */
   TEST_CHECK(note_write(0x30002ABC));
   TEST_CHECK(rsx_page_locks_pending());
   TEST_CHECK(rsx_page_locks_take(&page) && page == 0x30002000);
   TEST_CHECK(!rsx_page_locks_pending());
   TEST_CHECK(!rsx_page_locks_take(&page));

   /* Something to be written back has the page too: declined */
   rsx_page_locks_add(0x30002000, 0x30002FFF, 1);
   counts(0x30002000, 1, 1);
   TEST_CHECK(!note_write(0x30002ABC));
   TEST_CHECK(note_write(0x30001000));
   TEST_CHECK(note_write(0x30003FFF));
   TEST_CHECK(rsx_page_locks_pending());

   /* No-access only: declined */
   rsx_page_locks_remove(0x30001000, 0x30003FFF, 0);
   counts(0x30002000, 0, 1);
   counts(0x30001000, 0, 0);
   TEST_CHECK(!note_write(0x30002ABC));
   rsx_page_locks_remove(0x30002000, 0x30002FFF, 1);
   counts(0x30002000, 0, 0);

   /* The first and the last page of memory */
   rsx_page_locks_add(0, 0xFFF, 0);
   rsx_page_locks_add(0xFFFFF000u, 0xFFFFFFFFu, 0);
   rsx_page_locks_add(0xFFFFF000u, 0xFFFFFFFFu, 0);
   counts(0, 1, 0);
   counts(0xFFFFFFFFu, 2, 0);
   TEST_CHECK(note_write(0xFFFFFFFFu));

   /* A page somebody else holds is not unlocked by a guest write, with
    * textures on it or without; holding and releasing twice is once, and
    * the counts are not touched */
   rsx_page_locks_add(0x31000000, 0x31001FFF, 0);
   rsx_page_locks_hold(0x31001000, 0x31001FFF);
   rsx_page_locks_hold(0x31001000, 0x31001FFF);
   counts(0x31001000, 1, 0);
   TEST_CHECK(!rsx_page_locks_begin_write(0x31001234));
   TEST_CHECK(note_write(0x31000234));
   rsx_page_locks_release(0x31001000, 0x31001FFF);
   rsx_page_locks_release(0x31001000, 0x31001FFF);
   counts(0x31001000, 1, 0);
   TEST_CHECK(note_write(0x31001234));

   /* A page in hand: a second fault there is not taken a second time, and
    * the counts read as they are */
   TEST_CHECK(rsx_page_locks_begin_write(0x31000000) == 1);
   counts(0x31000000, 1, 0);
   rsx_page_locks_add(0x31000000, 0x31000FFF, 1);
   counts(0x31000000, 1, 1);
   TEST_CHECK(rsx_page_locks_end_write(0x31000000));
   TEST_CHECK(!rsx_page_locks_begin_write(0x31000000));
   rsx_page_locks_settle(0x31000000, 0x31001FFF);

   /* Emptied, but not what is held */
   rsx_page_locks_hold(0x31001000, 0x31001FFF);
   rsx_page_locks_reset();
   counts(0x31000000, 0, 0);
   rsx_page_locks_add(0x31001000, 0x31001FFF, 0);
   TEST_CHECK(!rsx_page_locks_begin_write(0x31001234));
   rsx_page_locks_release(0x31001000, 0x31001FFF);
   TEST_CHECK(note_write(0x31001234));

   /* Emptied: no counts, no notes */
   rsx_page_locks_reset();
   counts(0, 0, 0);
   counts(0xFFFFFFFFu, 0, 0);
   TEST_CHECK(!rsx_page_locks_pending());
   TEST_CHECK(!rsx_page_locks_take(&page));

   /* Room for RSX_PAGE_NOTES notes; past that a fault is declined and the
    * notes there stay */
   rsx_page_locks_add(0x40000000, 0x40000000 + (RSX_PAGE_NOTES + 8) * 4096u - 1, 0);
   for (i = 0; i < RSX_PAGE_NOTES + 8; i++)
      declined += !note_write(0x40000000 + i * 4096u);
   TEST_CHECK(declined == 8);
   for (i = 0; rsx_page_locks_take(&page); i++)
      ;
   TEST_CHECK(i == RSX_PAGE_NOTES);
   TEST_CHECK(!rsx_page_locks_pending());
   rsx_page_locks_reset();
}

#define WRITERS 4
#define PAGES   256       /* each writer's own */
#define ROUNDS  20000

static retro_atomic_int_t left[WRITERS * PAGES];   /* notes left, per page */
static retro_atomic_int_t taken[WRITERS * PAGES];
static retro_atomic_int_t bad;
static retro_atomic_int_t done;

static uint32_t address_of(uint32_t n) { return 0x50000000u + n * 4096u; }

static void *writer(void *arg)
{
   uint32_t t = (uint32_t)(size_t)arg;
   uint32_t x = t * 2654435761u + 1;
   uint32_t i;

   for (i = 0; i < ROUNDS; i++)
   {
      uint32_t n;
      x = x * 1664525u + 1013904223u;
      n = t * PAGES + (x >> 16) % PAGES;

      if (note_write(address_of(n) + (x & 0xFFF)))
         retro_atomic_fetch_add_int(&left[n], 1);
   }

   return NULL;
}

/* Locks and unlocks ranges across everybody's pages, no-access among
 * them: writers are declined meanwhile, never confused */
static void *locker(void *arg)
{
   uint32_t x = (uint32_t)(size_t)arg * 97u + 5;
   uint32_t i;

   for (i = 0; i < ROUNDS; i++)
   {
      uint32_t first, count;
      int na;
      x     = x * 1664525u + 1013904223u;
      first = (x >> 12) % (WRITERS * PAGES - 16);
      count = 1 + (x >> 4) % 16;
      na    = (int)(x >> 30 & 1);
      rsx_page_locks_add(address_of(first), address_of(first + count) - 1, na);
      rsx_page_locks_remove(address_of(first), address_of(first + count) - 1, na);
   }

   return NULL;
}

static void *taker(void *arg)
{
   uint32_t page;
   (void)arg;

   for (;;)
   {
      int was_done = retro_atomic_load_acquire_int(&done);

      while (rsx_page_locks_take(&page))
      {
         uint32_t n = (page - 0x50000000u) / 4096u;
         if (page & 0xFFF || n >= WRITERS * PAGES)
            retro_atomic_fetch_add_int(&bad, 1);
         else
            retro_atomic_fetch_add_int(&taken[n], 1);
      }

      if (was_done)
         break;
   }

   return NULL;
}

static void together(void)
{
   pthread_t w[WRITERS], l[2], k;
   uint32_t n;
   size_t t;
   int differ = 0, total = 0;

   rsx_page_locks_reset();
   rsx_page_locks_add(address_of(0), address_of(WRITERS * PAGES) - 1, 0);

   pthread_create(&k, NULL, taker, NULL);
   for (t = 0; t < 2; t++)
      pthread_create(&l[t], NULL, locker, (void*)t);
   for (t = 0; t < WRITERS; t++)
      pthread_create(&w[t], NULL, writer, (void*)t);
   for (t = 0; t < WRITERS; t++)
      pthread_join(w[t], NULL);
   for (t = 0; t < 2; t++)
      pthread_join(l[t], NULL);
   retro_atomic_store_release_int(&done, 1);
   pthread_join(k, NULL);

   for (n = 0; n < WRITERS * PAGES; n++)
   {
      differ += retro_atomic_load_acquire_int(&left[n]) != retro_atomic_load_acquire_int(&taken[n]);
      total  += retro_atomic_load_acquire_int(&left[n]);
   }

   TEST_CHECK(retro_atomic_load_acquire_int(&bad) == 0);
   TEST_CHECK(differ == 0);
   TEST_CHECK(total > ROUNDS);
   TEST_CHECK(!rsx_page_locks_pending());
   counts(address_of(0), 1, 0);
   counts(address_of(WRITERS * PAGES - 1), 1, 0);
   counts(address_of(WRITERS * PAGES / 2), 1, 0);
   rsx_page_locks_reset();
}

/* Page protections against faults, as the texture cache and the guest
 * threads go about them. A page's protection is a word here, set where
 * the real thing calls the host: 0 writable, 1 read-only, 2 no-access.
 *
 * One thread is the renderer. It locks pages for textures (counted, then
 * settled, then protected read-only), locks them for something that has
 * to be written back (the same, no-access), unlocks, and now and then
 * enters the cache: takes the notes and unlocks the textures of their
 * pages. Three threads are the game: a write to a page that is not
 * writable faults, and the fault is handled as texture_cache.h does it -
 * begin_write, make the page writable, end_write.
 *
 * - A page locked no-access stays no-access until the renderer unlocks
 *   it, whatever the game does meanwhile.
 * - Whenever the game stands still and the renderer has taken every note,
 *   each page it has locked for a texture is read-only: none was left
 *   writable with nobody told. */
#define MPAGES 6
#define MROUNDS 40000

static retro_atomic_int_t prot[MPAGES];
static retro_atomic_int_t hold_game, hold_round, parked_for[3], model_done, model_bad;
static retro_atomic_int_t game_notes;
static int have_ro[MPAGES], have_na[MPAGES];   /* the renderer's own */

static uint32_t maddr(uint32_t n) { return 0x60000000u + n * 4096u; }

static void *game(void *arg)
{
   size_t me  = (size_t)arg;
   uint32_t x = (uint32_t)me * 2246822519u + 3;

   while (!retro_atomic_load_acquire_int(&model_done))
   {
      uint32_t n;

      if (retro_atomic_load_acquire_int(&hold_game))
      {
         /* Parked for this standstill, by its number: a count of parked
          * threads would still count one that is on its way out of the
          * standstill before */
         int round = retro_atomic_load_acquire_int(&hold_round);
         retro_atomic_store_release_int(&parked_for[me], round);
         while (retro_atomic_load_acquire_int(&hold_game)
               && retro_atomic_load_acquire_int(&hold_round) == round)
            sched_yield();
         continue;
      }

      x = x * 1664525u + 1013904223u;
      n = (x >> 16) % MPAGES;

      if (!retro_atomic_load_acquire_int(&prot[n]))
         continue;                     /* the write goes through */

      if (rsx_page_locks_begin_write(maddr(n) + (x & 0xFFF)) == 1)
      {
         if (x & 0x100)
            sched_yield();
         retro_atomic_store_release_int(&prot[n], 0);
         if (rsx_page_locks_end_write(maddr(n)))
            retro_atomic_fetch_add_int(&game_notes, 1);
      }
   }

   return NULL;
}

static void take_notes(void)
{
   uint32_t page;

   while (rsx_page_locks_take(&page))
   {
      uint32_t n = (page - 0x60000000u) / 4096u;

      if (n < MPAGES && have_ro[n])
      {
         /* The texture is stale: unlocked, as an invalidation does */
         if (!have_na[n])
            retro_atomic_store_release_int(&prot[n], 0);
         rsx_page_locks_remove(maddr(n), maddr(n) + 4095, 0);
         have_ro[n] = 0;
      }
   }
}

/* Until the game has left its share of notes, the renderer waits for one
 * whenever there is a page the game can fault on: on a machine busy with
 * something else it otherwise gets through all its rounds with the game
 * hardly having run, and the run has then tested nothing (it failed that
 * check under load) */
static void wait_for_the_game(void)
{
   int before = retro_atomic_load_acquire_int(&game_notes);
   uint32_t n;

   if (before > 200)
      return;

   for (;;)
   {
      for (n = 0; n < MPAGES; n++)
      {
         if (retro_atomic_load_acquire_int(&prot[n]) == 1)
            break;
      }

      if (n == MPAGES || retro_atomic_load_acquire_int(&game_notes) != before)
         return;

      sched_yield();
   }
}

static void model(void)
{
   pthread_t g[3];
   uint32_t x = 12345, i, n;
   size_t t;
   int checks = 0, na_locks = 0, standstill = 0;

   rsx_page_locks_reset();
   for (t = 0; t < 3; t++)
      pthread_create(&g[t], NULL, game, (void*)t);

   for (i = 0; i < MROUNDS; i++)
   {
      wait_for_the_game();

      x = x * 1664525u + 1013904223u;
      n = (x >> 16) % MPAGES;

      switch ((x >> 8) % 8)
      {
      case 0: case 1: case 2:
         if (!have_ro[n])
         {
            rsx_page_locks_add(maddr(n), maddr(n) + 4095, 0);
            rsx_page_locks_settle(maddr(n), maddr(n) + 4095);
            if (!have_na[n])
               retro_atomic_store_release_int(&prot[n], 1);
            have_ro[n] = 1;
         }
         break;
      case 3: case 4:
         if (!have_na[n])
         {
            int spin;
            rsx_page_locks_add(maddr(n), maddr(n) + 4095, 1);
            rsx_page_locks_settle(maddr(n), maddr(n) + 4095);
            retro_atomic_store_release_int(&prot[n], 2);
            have_na[n] = 1;
            na_locks++;

            /* It holds, however the game is going at it */
            for (spin = 0; spin < 50; spin++)
            {
               if (retro_atomic_load_acquire_int(&prot[n]) != 2)
                  retro_atomic_fetch_add_int(&model_bad, 1);
            }
         }
         break;
      case 5:
         if (have_na[n])
         {
            if (retro_atomic_load_acquire_int(&prot[n]) != 2)
               retro_atomic_fetch_add_int(&model_bad, 1);
            retro_atomic_store_release_int(&prot[n], have_ro[n] ? 1 : 0);
            rsx_page_locks_remove(maddr(n), maddr(n) + 4095, 1);
            have_na[n] = 0;
         }
         break;
      case 6:
         take_notes();
         break;
      default:
         /* The game stands still; every note taken; what is locked for a
          * texture is read-only (or no-access, with both) */
         standstill++;
         retro_atomic_store_release_int(&hold_round, standstill);
         retro_atomic_store_release_int(&hold_game, 1);
         while (retro_atomic_load_acquire_int(&parked_for[0]) != standstill
               || retro_atomic_load_acquire_int(&parked_for[1]) != standstill
               || retro_atomic_load_acquire_int(&parked_for[2]) != standstill)
            sched_yield();
         take_notes();
         for (n = 0; n < MPAGES; n++)
         {
            int want = have_na[n] ? 2 : have_ro[n] ? 1 : 0;
            if (retro_atomic_load_acquire_int(&prot[n]) != want)
               retro_atomic_fetch_add_int(&model_bad, 1);
         }
         checks++;
         retro_atomic_store_release_int(&hold_game, 0);
         break;
      }
   }

   retro_atomic_store_release_int(&model_done, 1);
   for (t = 0; t < 3; t++)
      pthread_join(g[t], NULL);

   TEST_CHECK(retro_atomic_load_acquire_int(&model_bad) == 0);
   TEST_CHECK(checks > 100 && na_locks > 100);
   TEST_CHECK(retro_atomic_load_acquire_int(&game_notes) > 100);
   printf("test_guest_page_locks: %d notes left by the game against %d no-access locks, %d checks at a standstill\n",
         retro_atomic_load_acquire_int(&game_notes), na_locks, checks);
   rsx_page_locks_reset();
}

int main(void)
{
   /* A lock that is broken can leave a page in a guest thread's hand for
    * good, and the renderer then waits for it for ever: that is a failure,
    * not a run that never ends */
   alarm(300);
   alone();
   model();
   together();
   printf("test_guest_page_locks: %s\n", test_failures ? "FAIL" : "ok");
   return test_failures ? 1 : 0;
}
