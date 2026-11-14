/* Faults a guest thread hands to the renderer (guest_faults.h) */

#include "guest_faults.h"

#include <stddef.h>
#include <rthreads/retro_waitaddr.h>

/* The list's head: NULL while there is no renderer, the address of
 * faults_none while there is one and nothing is posted, else the fault
 * posted last. faults_none also ends the list. */
static rsx_guest_fault_t  faults_none;
static retro_atomic_ptr_t faults_head;

#define FAULTS_NONE ((void*)&faults_none)

void rsx_guest_faults_open(void)
{
   retro_atomic_cas_ptr(&faults_head, NULL, FAULTS_NONE);
}

int rsx_guest_faults_close(void)
{
   if (retro_atomic_cas_ptr(&faults_head, FAULTS_NONE, NULL))
      return 1;
   return !retro_atomic_load_acquire_ptr(&faults_head);
}

int rsx_guest_faults_post(rsx_guest_fault_t *fault, uint32_t address,
      int writing)
{
   fault->address = address;
   fault->writing = writing;
   fault->handled = 0;
   retro_atomic_store_relaxed_int(&fault->done, 0);

   for (;;)
   {
      void *head = retro_atomic_load_acquire_ptr(&faults_head);

      if (!head)
         return 0;

      fault->next = (rsx_guest_fault_t*)head;

      if (retro_atomic_cas_ptr(&faults_head, head, fault))
         return 1;
   }
}

int rsx_guest_fault_wait(rsx_guest_fault_t *fault)
{
   retro_waitaddr_t on;

   on.addr = &fault->done;
   on.old  = 0;

   while (!retro_atomic_load_acquire_int(&fault->done))
      retro_waitaddr_wait(&on, 1, -1);

   return fault->handled;
}

int rsx_guest_faults_pending(void)
{
   void *head = retro_atomic_load_acquire_ptr(&faults_head);
   return head && head != FAULTS_NONE;
}

rsx_guest_fault_t *rsx_guest_faults_take(void)
{
   rsx_guest_fault_t *fault;
   rsx_guest_fault_t *oldest = NULL;

   /* Only the renderer takes, and only it closes: a head that is a
    * fault stays one until this has it */
   for (;;)
   {
      void *head = retro_atomic_load_acquire_ptr(&faults_head);

      if (!head || head == FAULTS_NONE)
         return NULL;

      if (retro_atomic_cas_ptr(&faults_head, head, FAULTS_NONE))
      {
         fault = (rsx_guest_fault_t*)head;
         break;
      }
   }

   /* Posted last comes first: turned round */
   while (fault != &faults_none)
   {
      rsx_guest_fault_t *older = fault->next;

      fault->next = oldest;
      oldest      = fault;
      fault       = older;
   }

   return oldest;
}

void rsx_guest_fault_finish(rsx_guest_fault_t *fault, int handled)
{
   /* The record is on the waiting thread's stack and may be gone as soon
    * as done is set: its address is only a name after that */
   const void *name = &fault->done;

   fault->handled = handled;
   retro_atomic_store_release_int(&fault->done, 1);
   retro_waitaddr_notify_all(name);
}
