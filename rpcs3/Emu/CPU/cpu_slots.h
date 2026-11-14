#pragma once

/* The 128 slots of the threads that suspend_all pauses (CPUThread.cpp): a
 * 128-bit word, one bit a slot, taken and given back with one 128-bit
 * compare-and-swap. Lock-free. */

#include <retro_atomic.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CPU_SLOTS 128

/* Takes the lowest free slot: its number, or -1 when all are taken */
int cpu_slots_take(retro_atomic_128_t *slots);

/* Gives a slot back */
void cpu_slots_give(retro_atomic_128_t *slots, unsigned slot);

/* The slots taken, as they are now */
retro_u128_t cpu_slots_taken(retro_atomic_128_t *slots);

#ifdef __cplusplus
}
#endif
