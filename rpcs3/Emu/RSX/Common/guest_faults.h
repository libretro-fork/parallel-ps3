/* Faults a guest thread hands to the renderer - with no lock.
 *
 * A thread that faults on memory the texture cache has locked, and that
 * cannot deal with it alone (guest_page_locks.h), used to enter the cache
 * itself, under the cache's lock, beside the renderer. It now asks the
 * renderer: it posts the fault here and waits, and the renderer, between
 * two pieces of its own work, handles the fault as it would one of its
 * own and wakes the thread. Only the renderer is ever inside the cache.
 *
 * A fault is a record on the faulting thread's stack: nothing is
 * allocated, and posting is one compare-and-swap. */

#ifndef RSX_GUEST_FAULTS_H
#define RSX_GUEST_FAULTS_H

#include <stdint.h>
#include <retro_atomic.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rsx_guest_fault
{
   struct rsx_guest_fault *next;    /* for the renderer: the one posted after */
   uint32_t                address;
   int                     writing;
   int                     handled; /* the renderer's answer */
   retro_atomic_int_t      done;
} rsx_guest_fault_t;

/* The renderer is there to answer from now on. Until this is called, and
 * after rsx_guest_faults_close, nothing can be posted. */
void rsx_guest_faults_open(void);

/* The renderer is going. 1: closed, nothing more can be posted. 0: faults
 * are waiting - the caller takes and finishes them and calls again. */
int rsx_guest_faults_close(void);

/* Posts the fault. 0 when there is no renderer to answer: nothing was
 * posted, the caller deals with the fault itself. Otherwise the record
 * must stay where it is until rsx_guest_fault_wait has returned. */
int rsx_guest_faults_post(rsx_guest_fault_t *fault, uint32_t address,
      int writing);

/* Blocks until the renderer has finished the fault: its answer */
int rsx_guest_fault_wait(rsx_guest_fault_t *fault);

/* For the renderer. Nonzero when faults are waiting: one load */
int rsx_guest_faults_pending(void);

/* For the renderer: every fault posted so far, the oldest first, chained
 * by next; NULL when there is none. Read a fault's next before finishing
 * it. */
rsx_guest_fault_t *rsx_guest_faults_take(void);

/* For the renderer: gives the answer and wakes the thread. The record is
 * not the renderer's to touch after this. */
void rsx_guest_fault_finish(rsx_guest_fault_t *fault, int handled);

#ifdef __cplusplus
}
#endif

#endif
