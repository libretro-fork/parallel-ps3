/* See lf_hazard.h */

#include "lf_hazard.h"

#include <stddef.h>

#include <retro_atomic.h>

struct lf_hazard_record
{
   retro_atomic_ptr_t cells[LF_HAZARD_CELLS];
   retro_atomic_int_t claimed;
   /* A cache line to itself: no other thread writes here */
   char pad[64 - (LF_HAZARD_CELLS * sizeof(void*) + sizeof(int)) % 64];
};

static struct lf_hazard_record records[LF_HAZARD_THREADS];

/* No record from here up was ever claimed: as far as a search goes */
static retro_atomic_int_t records_used;

int lf_hazard_claim(void)
{
   int i;

   for (i = 0; i < LF_HAZARD_THREADS; i++)
   {
      if (retro_atomic_load_relaxed_int(&records[i].claimed))
         continue;

      if (retro_atomic_cas_int(&records[i].claimed, 0, 1))
      {
         int used = retro_atomic_load_acquire_int(&records_used);

         while (used < i + 1 && !retro_atomic_cas_int(&records_used, used, i + 1))
            used = retro_atomic_load_acquire_int(&records_used);

         return i;
      }
   }

   return -1;
}

void lf_hazard_release(int record)
{
   if (record >= 0 && record < LF_HAZARD_THREADS)
      retro_atomic_store_release_int(&records[record].claimed, 0);
}

int lf_hazard_hold(int record, void *p)
{
   struct lf_hazard_record *r = &records[record];
   int cell;

   for (cell = 0; cell < LF_HAZARD_CELLS; cell++)
   {
      if (retro_atomic_load_relaxed_ptr(&r->cells[cell]))
         continue;

      /* An exchange, for the full barrier: this store is seen by a
       * search that starts after it, and the caller's next load of the
       * table sees a removal that came before that search */
      (void)retro_atomic_exchange_ptr(&r->cells[cell], p);
      return cell;
   }

   return -1;
}

void lf_hazard_drop(int record, int cell)
{
   retro_atomic_store_release_ptr(&records[record].cells[cell], NULL);
}

int lf_hazard_any(const void *p)
{
   int used = retro_atomic_load_acquire_int(&records_used);
   int i, cell;

   for (i = 0; i < used; i++)
   {
      for (cell = 0; cell < LF_HAZARD_CELLS; cell++)
      {
         if (retro_atomic_load_acquire_ptr(&records[i].cells[cell]) == p)
            return 1;
      }
   }

   return 0;
}
