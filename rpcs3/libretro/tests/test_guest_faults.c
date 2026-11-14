/* Faults a guest thread hands to the renderer
 * (Emu/RSX/Common/guest_faults.c).
 *
 * - with no renderer nothing can be posted; with one, a fault posted is
 *   pending, is taken once, and its thread gets the answer given for it;
 *   a request to let go of unmapped memory is told apart from a fault;
 * - faults are taken in the order they were posted;
 * - the renderer cannot close while a fault waits, and nothing can be
 *   posted once it has closed;
 * - four threads post faults of their own while a fifth answers them,
 *   and at the end closes under them: every fault posted is answered
 *   once, with the answer for that fault, each thread's in the order it
 *   posted them, and every thread ends - none is left waiting; if one
 *   is, the test is killed after five minutes and so fails. (Built a
 *   second time with ThreadSanitizer.)
 *
 * Usage: test_guest_faults */

#include "../../Emu/RSX/Common/guest_faults.h"
#include <stdio.h>
#include <stddef.h>

#include <pthread.h>
#include <sched.h>
#include <unistd.h>

static int test_failures;

#define CHECK(cond, what) do { if (!(cond)) { printf("FAIL: %s\n", what); test_failures++; } } while (0)

#define THREADS 4
#define FAULTS  20000

static retro_atomic_int_t stop_serving;
static retro_atomic_int_t posted_total;
static retro_atomic_int_t refused_total;
static int                served_total;
static int                bad_order;
static uint32_t           last_of[THREADS];

static int answer_for(uint32_t address, int writing)
{
   return (int)((address * 7u + (uint32_t)writing) % 5u);
}

static void serve(void)
{
   rsx_guest_fault_t *f = rsx_guest_faults_take();

   while (f)
   {
      rsx_guest_fault_t *next = f->next;
      uint32_t who            = f->address >> 24;
      uint32_t n              = f->address & 0xffffff;

      if (n != last_of[who] + 1)
         bad_order++;
      last_of[who] = n;
      served_total++;

      rsx_guest_fault_finish(f, answer_for(f->address, f->writing));
      f = next;
   }
}

static void *server(void *arg)
{
   (void)arg;

   while (!retro_atomic_load_acquire_int(&stop_serving))
   {
      if (rsx_guest_faults_pending())
         serve();
      else
         sched_yield();
   }

   /* Going: what is still posted is answered first */
   while (!rsx_guest_faults_close())
      serve();

   return NULL;
}

static void *poster(void *arg)
{
   uint32_t who = (uint32_t)(size_t)arg;
   uint32_t n;
   size_t   wrong = 0;

   for (n = 1; n <= FAULTS; n++)
   {
      rsx_guest_fault_t f;
      uint32_t address = (who << 24) | n;
      int writing      = (int)(n & 1);

      if (!rsx_guest_faults_post(&f, address, writing))
      {
         retro_atomic_inc_int(&refused_total);
         continue;
      }

      retro_atomic_inc_int(&posted_total);

      if (rsx_guest_fault_wait(&f) != answer_for(address, writing))
         wrong++;

      if (n == FAULTS / 2 && who == 0)
         retro_atomic_store_release_int(&stop_serving, 1);
   }

   return (void*)wrong;
}

int main(void)
{
   rsx_guest_fault_t a, b, c;
   rsx_guest_fault_t *f;
   pthread_t srv;
   pthread_t th[THREADS];
   size_t wrong = 0;
   int i;

   /* A thread left waiting is a failure, not a test that never ends */
   alarm(300);

   CHECK(!rsx_guest_faults_post(&a, 0x1000, 1), "posted with no renderer");
   CHECK(!rsx_guest_faults_pending(), "pending with no renderer");
   CHECK(rsx_guest_faults_close() == 1, "closing twice");

   rsx_guest_faults_open();
   CHECK(!rsx_guest_faults_pending(), "pending when nothing is posted");
   CHECK(!rsx_guest_faults_take(), "took a fault when none is posted");

   CHECK(rsx_guest_faults_post(&a, 0x1000, 1), "post a");
   CHECK(rsx_guest_faults_post(&b, 0x2000, 0), "post b");
   CHECK(rsx_guest_faults_post(&c, 0, RSX_GUEST_MEMORY_UNMAPPED), "post c");
   CHECK(rsx_guest_faults_pending(), "three posted, none pending");
   CHECK(rsx_guest_faults_close() == 0, "closed over waiting faults");

   f = rsx_guest_faults_take();
   CHECK(f == &a && f->next == &b && b.next == &c && !c.next,
         "faults not taken in the order they were posted");
   CHECK(a.address == 0x1000 && a.writing == 1 && b.writing == 0
         && c.writing == RSX_GUEST_MEMORY_UNMAPPED,
         "a fault's address or kind");
   CHECK(!rsx_guest_faults_pending() && !rsx_guest_faults_take(),
         "faults taken twice");

   rsx_guest_fault_finish(&a, 1);
   rsx_guest_fault_finish(&b, 0);
   rsx_guest_fault_finish(&c, 3);
   CHECK(rsx_guest_fault_wait(&a) == 1 && rsx_guest_fault_wait(&b) == 0
         && rsx_guest_fault_wait(&c) == 3, "a finished fault's answer");

   CHECK(rsx_guest_faults_close() == 1, "close with nothing waiting");
   CHECK(!rsx_guest_faults_post(&a, 0x1000, 1), "posted after the close");
   CHECK(!rsx_guest_faults_pending(), "pending after the close");

   /* Together */
   rsx_guest_faults_open();
   pthread_create(&srv, NULL, server, NULL);
   for (i = 0; i < THREADS; i++)
      pthread_create(&th[i], NULL, poster, (void*)(size_t)i);
   for (i = 0; i < THREADS; i++)
   {
      void *r;
      pthread_join(th[i], &r);
      wrong += (size_t)r;
   }
   pthread_join(srv, NULL);

   CHECK(wrong == 0, "a thread got another fault's answer");
   CHECK(bad_order == 0, "a thread's faults answered out of order");
   CHECK(served_total == retro_atomic_load_acquire_int(&posted_total),
         "faults posted and faults answered differ");
   CHECK(retro_atomic_load_acquire_int(&posted_total) >= FAULTS / 2,
         "too few faults went through to test anything");
   CHECK(retro_atomic_load_acquire_int(&refused_total) > 0,
         "no post was refused after the close");
   CHECK(!rsx_guest_faults_post(&a, 0x1000, 1), "posted after the last close");

   printf("guest faults: %d answered, %d refused after the close\n",
         served_total, retro_atomic_load_acquire_int(&refused_total));
   printf("test_guest_faults: %s\n", test_failures ? "FAIL" : "ok");
   return test_failures ? 1 : 0;
}
