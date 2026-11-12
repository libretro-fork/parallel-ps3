/* The libretro swapchain's image bookkeeping (Emu/RSX/VK/vkutils/
 * libretro_images.h), as swapchain_LIBRETRO shares it between the
 * renderer's thread and retro_run without a lock:
 *
 * - images are taken once each until given back, and a full set takes no
 *   more;
 * - a renderer taking images and a retro_run giving them back on two
 *   threads: no image is ever held twice;
 * - retired sets pushed by one thread and freed by another are each freed
 *   once, none lost.
 *
 * Built twice, under AddressSanitizer and under ThreadSanitizer. */

#include <pthread.h>
#include <sched.h>

#include "../../Emu/RSX/VK/vkutils/libretro_images.h"
#include "test_common.h"

static struct lr_image_slots slots;
static retro_atomic_int_t owner[LR_MAX_IMAGES];
static retro_atomic_int_t mailbox[LR_MAX_IMAGES * 4];
static retro_atomic_int_t produced, consumed, doubles;

#define ROUNDS 200000

/* The renderer: takes a free image, marks it its own, hands it over */
static void *renderer(void *arg)
{
   int n = 0;
   (void)arg;
   while (n < ROUNDS)
   {
      int index = lr_image_slots_acquire(&slots);
      if (index < 0)
      {
         sched_yield();
         continue;
      }
      if (retro_atomic_fetch_add_int(&owner[index], 1) != 0)
         retro_atomic_fetch_add_int(&doubles, 1);
      /* hand over: wait for a free mailbox entry */
      while (retro_atomic_load_acquire_int(&produced) - retro_atomic_load_acquire_int(&consumed) >= LR_MAX_IMAGES * 4)
         sched_yield();
      retro_atomic_store_release_int(&mailbox[n % (LR_MAX_IMAGES * 4)], index);
      retro_atomic_fetch_add_int(&produced, 1);
      n++;
   }
   return NULL;
}

static int freed[1000];
static retro_atomic_ptr_t retired;
static struct lr_retired nodes[1000];

static void free_node(void *owner_ptr)
{
   freed[(struct lr_retired*)owner_ptr - nodes]++;
}

static void *retirer(void *arg)
{
   int i;
   (void)arg;
   for (i = 0; i < 1000; i++)
   {
      nodes[i].free_cb = free_node;
      nodes[i].owner   = &nodes[i];
      lr_retired_push(&retired, &nodes[i]);
      if (i % 7 == 0)
         sched_yield();
   }
   return NULL;
}

int main(void)
{
   pthread_t t;
   int i, n, lost = 0, twice = 0;

   /* One thread */
   lr_image_slots_init(&slots, 8);
   for (i = 0; i < 8; i++)
      TEST_CHECK(lr_image_slots_acquire(&slots) == i);
   TEST_CHECK(lr_image_slots_acquire(&slots) == -1);
   lr_image_slots_release(&slots, 5);
   TEST_CHECK(!lr_image_slots_held(&slots, 5));
   TEST_CHECK(lr_image_slots_acquire(&slots) == 5);
   lr_image_slots_release(&slots, 99); /* out of range: nothing */
   lr_image_slots_release_all(&slots);
   TEST_CHECK(lr_image_slots_acquire(&slots) == 0);
   lr_image_slots_init(&slots, 3);
   TEST_CHECK(lr_image_slots_acquire(&slots) == 0);
   TEST_CHECK(lr_image_slots_acquire(&slots) == 1);
   TEST_CHECK(lr_image_slots_acquire(&slots) == 2);
   TEST_CHECK(lr_image_slots_acquire(&slots) == -1);

   /* Two threads: retro_run gives back what the renderer hands over */
   lr_image_slots_init(&slots, LR_MAX_IMAGES);
   for (i = 0; i < LR_MAX_IMAGES; i++)
      retro_atomic_int_init(&owner[i], 0);
   retro_atomic_int_init(&produced, 0);
   retro_atomic_int_init(&consumed, 0);
   retro_atomic_int_init(&doubles, 0);
   pthread_create(&t, NULL, renderer, NULL);
   for (n = 0; n < ROUNDS; n++)
   {
      int index;
      while (retro_atomic_load_acquire_int(&produced) == n)
         sched_yield();
      index = retro_atomic_load_acquire_int(&mailbox[n % (LR_MAX_IMAGES * 4)]);
      retro_atomic_fetch_sub_int(&owner[index], 1);
      retro_atomic_fetch_add_int(&consumed, 1);
      lr_image_slots_release(&slots, (unsigned)index);
   }
   pthread_join(t, NULL);
   TEST_CHECK(retro_atomic_load_acquire_int(&doubles) == 0);

   /* Retired sets: pushed on one thread, freed on this one */
   retro_atomic_ptr_init(&retired, NULL);
   pthread_create(&t, NULL, retirer, NULL);
   for (i = 0; i < 2000; i++)
   {
      lr_retired_free_all(&retired);
      sched_yield();
   }
   pthread_join(t, NULL);
   lr_retired_free_all(&retired);
   TEST_CHECK(!lr_retired_any(&retired));
   for (i = 0; i < 1000; i++)
   {
      lost  += freed[i] == 0;
      twice += freed[i] > 1;
   }
   TEST_CHECK(lost == 0 && twice == 0);

   printf("vk images: %d images handed over, %d held twice; 1000 sets retired, %d lost, %d freed twice\n",
         ROUNDS, retro_atomic_load_acquire_int(&doubles), lost, twice);
   if (test_failures)
      printf("vk images: FAIL\n");
   return test_failures ? 1 : 0;
}
