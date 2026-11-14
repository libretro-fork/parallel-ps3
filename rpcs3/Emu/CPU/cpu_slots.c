#include "cpu_slots.h"

#if defined(_MSC_VER)
#include <intrin.h>
#endif

static unsigned lowest_set(uint64_t v)
{
#if defined(_MSC_VER)
   unsigned long i;
   _BitScanForward64(&i, v);
   return (unsigned)i;
#else
   return (unsigned)__builtin_ctzll(v);
#endif
}

int cpu_slots_take(retro_atomic_128_t *slots)
{
   const uint64_t all = ~(uint64_t)0;
   retro_u128_t seen  = retro_atomic_load_acquire_128(slots);

   for (;;)
   {
      retro_u128_t want = seen;
      unsigned slot;

      if (seen.lo != all)
      {
         slot    = lowest_set(~seen.lo);
         want.lo = seen.lo | (seen.lo + 1);
      }
      else if (seen.hi != all)
      {
         slot    = 64 + lowest_set(~seen.hi);
         want.hi = seen.hi | (seen.hi + 1);
      }
      else
         return -1;

      /* A failed swap leaves what is there in seen */
      if (retro_atomic_cas_seen_128(slots, &seen, want))
         return (int)slot;
   }
}

void cpu_slots_give(retro_atomic_128_t *slots, unsigned slot)
{
   retro_u128_t seen = retro_atomic_load_acquire_128(slots);

   slot %= CPU_SLOTS;

   for (;;)
   {
      retro_u128_t want = seen;

      if (slot < 64)
         want.lo &= ~((uint64_t)1 << slot);
      else
         want.hi &= ~((uint64_t)1 << (slot - 64));

      if (retro_atomic_cas_seen_128(slots, &seen, want))
         return;
   }
}

retro_u128_t cpu_slots_taken(retro_atomic_128_t *slots)
{
   return retro_atomic_load_acquire_128(slots);
}
