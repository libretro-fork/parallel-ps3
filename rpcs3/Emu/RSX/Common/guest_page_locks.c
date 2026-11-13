/* See guest_page_locks.h */

#include "guest_page_locks.h"

#include <retro_atomic.h>

#if defined(__SSE2__) || defined(_M_X64) || defined(_M_IX86)
#include <emmintrin.h>
#endif

/* 4 GiB of guest addresses in 4 KiB pages. A page's word: sections that
 * have it locked read-only in the low 14 bits, no-access in the high
 * half, and between them the bit that says somebody other than the
 * texture cache has the page locked (rsx_page_locks_hold) and the bit that
 * says a guest thread has the page in hand (rsx_page_locks_begin_write). */
#define PAGE_SHIFT 12
#define PAGE_COUNT (1u << 20)
#define RO_ONE     1
#define HELD       (1 << 14)
#define IN_HAND    (1 << 15)
#define NA_ONE     (1 << 16)
#define RO_OF(w)   ((uint32_t)(w) & 0x3FFFu)
#define NA_OF(w)   ((uint32_t)(w) >> 16)

static retro_atomic_int_t page_locks[PAGE_COUNT];

/* A note is the page's number plus one; 0 is an empty slot */
static retro_atomic_int_t notes[RSX_PAGE_NOTES];
static retro_atomic_int_t notes_waiting;

void rsx_page_locks_reset(void)
{
   uint32_t i;

   for (i = 0; i < PAGE_COUNT; i++)
   {
      /* Most were never touched: leave those pages of the table alone.
       * What somebody else holds is theirs to release. */
      int word = retro_atomic_load_relaxed_int(&page_locks[i]);

      if (word & ~HELD)
         retro_atomic_fetch_and_int(&page_locks[i], HELD);
   }

   for (i = 0; i < RSX_PAGE_NOTES; i++)
      retro_atomic_store_relaxed_int(&notes[i], 0);

   retro_atomic_store_release_int(&notes_waiting, 0);
}

void rsx_page_locks_add(uint32_t first, uint32_t last, int no_access)
{
   uint32_t i;
   uint32_t end = last >> PAGE_SHIFT;

   for (i = first >> PAGE_SHIFT; i <= end; i++)
      retro_atomic_fetch_add_int(&page_locks[i], no_access ? NA_ONE : RO_ONE);
}

void rsx_page_locks_remove(uint32_t first, uint32_t last, int no_access)
{
   uint32_t i;
   uint32_t end = last >> PAGE_SHIFT;

   for (i = first >> PAGE_SHIFT; i <= end; i++)
      retro_atomic_fetch_sub_int(&page_locks[i], no_access ? NA_ONE : RO_ONE);
}

static void pause_a_moment(void)
{
#if defined(__SSE2__) || defined(_M_X64) || defined(_M_IX86)
   _mm_pause();
#endif
}

int rsx_page_locks_begin_write(uint32_t address)
{
   uint32_t page = address >> PAGE_SHIFT;
   int waited    = 0;

   for (;;)
   {
      int word = retro_atomic_load_acquire_int(&page_locks[page]);

      if (word & IN_HAND)
      {
         /* Another guest thread's: it is a protection call from done */
         waited = 1;
         pause_a_moment();
         continue;
      }

      if (waited)
         return 2;

      if (!RO_OF(word) || NA_OF(word) || (word & HELD))
         return 0;

      if (retro_atomic_cas_int(&page_locks[page], word, word | IN_HAND))
         return 1;
   }
}

int rsx_page_locks_end_write(uint32_t address)
{
   uint32_t page = address >> PAGE_SHIFT;
   uint32_t i;
   int left = 0;

   for (i = 0; i < RSX_PAGE_NOTES; i++)
   {
      if (retro_atomic_load_relaxed_int(&notes[i]))
         continue;

      if (retro_atomic_cas_int(&notes[i], 0, (int)(page + 1)))
      {
         retro_atomic_fetch_add_int(&notes_waiting, 1);
         left = 1;
         break;
      }
   }

   /* The note is there before the page is given up: a lock that waited
    * for the page finds it */
   retro_atomic_fetch_sub_int(&page_locks[page], IN_HAND);
   return left;
}

static void set_held(uint32_t first, uint32_t last, int held)
{
   uint32_t i;
   uint32_t end = last >> PAGE_SHIFT;

   for (i = first >> PAGE_SHIFT; i <= end; i++)
   {
      for (;;)
      {
         int word = retro_atomic_load_acquire_int(&page_locks[i]);
         int want = held ? (word | HELD) : (word & ~HELD);

         if (word == want || retro_atomic_cas_int(&page_locks[i], word, want))
            break;
      }
   }
}

void rsx_page_locks_hold(uint32_t first, uint32_t last)
{
   set_held(first, last, 1);
}

void rsx_page_locks_release(uint32_t first, uint32_t last)
{
   set_held(first, last, 0);
}

void rsx_page_locks_settle(uint32_t first, uint32_t last)
{
   uint32_t i;
   uint32_t end = last >> PAGE_SHIFT;

   for (i = first >> PAGE_SHIFT; i <= end; i++)
   {
      while (retro_atomic_load_acquire_int(&page_locks[i]) & IN_HAND)
         pause_a_moment();
   }
}

int rsx_page_locks_pending(void)
{
   return retro_atomic_load_acquire_int(&notes_waiting);
}

int rsx_page_locks_take(uint32_t *page_address)
{
   uint32_t i;

   if (!retro_atomic_load_acquire_int(&notes_waiting))
      return 0;

   for (i = 0; i < RSX_PAGE_NOTES; i++)
   {
      int note;

      if (!retro_atomic_load_relaxed_int(&notes[i]))
         continue;

      note = retro_atomic_exchange_int(&notes[i], 0);

      if (note)
      {
         retro_atomic_fetch_sub_int(&notes_waiting, 1);
         *page_address = ((uint32_t)note - 1) << PAGE_SHIFT;
         return 1;
      }
   }

   return 0;
}

void rsx_page_locks_get(uint32_t address, uint32_t *read_only,
      uint32_t *no_access)
{
   int word   = retro_atomic_load_acquire_int(&page_locks[address >> PAGE_SHIFT]);
   *read_only = RO_OF(word);
   *no_access = NA_OF(word);
}
