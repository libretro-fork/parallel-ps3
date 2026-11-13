/* The table of buffers that mirror guest memory
 * (Emu/RSX/Common/dma_views.c).
 *
 * - an address finds the view set over its 64 KiB block and no other; a
 *   range is held by a view only when all of it is inside;
 * - a copy into the buffers takes exactly the bytes of the range from
 *   guest memory, at the right offset, and a copy back puts them where
 *   they came from; a range that runs over two buffers is copied through
 *   both; a view with no mapping is passed over; a range that reaches
 *   memory no view covers copies what it can and says so;
 * - readers looking views up while they are replaced always see a view
 *   that is whole and covers the address. (Built a second time with
 *   ThreadSanitizer.)
 *
 * Usage: test_dma_views */

#include "../../Emu/RSX/Common/dma_views.h"
#include "test_common.h"

#include <pthread.h>
#include <retro_atomic.h>

#define BLOCK 0x10000u
#define GUEST (8 * BLOCK)

static uint8_t guest[GUEST];
static uint8_t buf_a[2 * BLOCK]; /* stands for blocks 1 and 2 */
static uint8_t buf_b[BLOCK];     /* block 3 */

static void fill(uint8_t *p, uint32_t n, uint32_t seed)
{
   uint32_t i;
   for (i = 0; i < n; i++)
      p[i] = (uint8_t)((i * 31u + seed) ^ (i >> 8));
}

static void alone(void)
{
   static struct rsx_dma_view a, b, own;
   static uint8_t before[GUEST];

   a.base = 1 * BLOCK; a.end = 3 * BLOCK - 1; a.buffer = &a; a.mapping = buf_a;
   b.base = 3 * BLOCK; b.end = 4 * BLOCK - 1; b.buffer = &b; b.mapping = buf_b;
   own.base = 5 * BLOCK; own.end = 6 * BLOCK - 1; own.buffer = &own; own.mapping = NULL;

   rsx_dma_views_clear();
   TEST_CHECK(rsx_dma_views_find(BLOCK) == NULL);
   TEST_CHECK(!rsx_dma_views_sync(BLOCK, 16, 1, guest));
   TEST_CHECK(rsx_dma_views_sync(BLOCK, 0, 1, guest));

   rsx_dma_views_set(a.base, a.end, &a);
   rsx_dma_views_set(b.base, b.end, &b);
   rsx_dma_views_set(own.base, own.end, &own);

   TEST_CHECK(rsx_dma_views_find(BLOCK - 1) == NULL);
   TEST_CHECK(rsx_dma_views_find(BLOCK) == &a);
   TEST_CHECK(rsx_dma_views_find(3 * BLOCK - 1) == &a);
   TEST_CHECK(rsx_dma_views_find(3 * BLOCK) == &b);
   TEST_CHECK(rsx_dma_views_find(4 * BLOCK) == NULL);
   TEST_CHECK(rsx_dma_views_find(5 * BLOCK + 7) == &own);
   TEST_CHECK(rsx_dma_views_find(0xFFFFFFFFu) == NULL);

   /* All of the range inside, or not held */
   TEST_CHECK(rsx_dma_views_find_range(BLOCK, 2 * BLOCK) == &a);
   TEST_CHECK(rsx_dma_views_find_range(BLOCK + 5, 100) == &a);
   TEST_CHECK(rsx_dma_views_find_range(BLOCK, 2 * BLOCK + 1) == NULL);
   TEST_CHECK(rsx_dma_views_find_range(3 * BLOCK - 1, 2) == NULL);
   TEST_CHECK(rsx_dma_views_find_range(3 * BLOCK, BLOCK) == &b);
   TEST_CHECK(rsx_dma_views_find_range(0xFFFFFFF0u, 0x20) == NULL);

   /* Into the buffer: those bytes, at their offset, and no others */
   fill(guest, GUEST, 1);
   memset(buf_a, 0xEE, sizeof(buf_a));
   memset(buf_b, 0xEE, sizeof(buf_b));
   TEST_CHECK(rsx_dma_views_sync(BLOCK + 100, 5000, 1, guest));
   TEST_CHECK(!memcmp(buf_a + 100, guest + BLOCK + 100, 5000));
   TEST_CHECK(buf_a[99] == 0xEE && buf_a[5100] == 0xEE);
   TEST_CHECK(buf_b[0] == 0xEE);

   /* Over the end of one buffer into the next */
   TEST_CHECK(rsx_dma_views_sync(3 * BLOCK - 300, 1000, 1, guest));
   TEST_CHECK(!memcmp(buf_a + 2 * BLOCK - 300, guest + 3 * BLOCK - 300, 300));
   TEST_CHECK(!memcmp(buf_b, guest + 3 * BLOCK, 700));
   TEST_CHECK(buf_b[700] == 0xEE);

   /* And back: the guest gets what the buffers hold, there and only there */
   fill(buf_a, sizeof(buf_a), 2);
   fill(buf_b, sizeof(buf_b), 3);
   memcpy(before, guest, GUEST);
   TEST_CHECK(rsx_dma_views_sync(3 * BLOCK - 300, 1000, 0, guest));
   TEST_CHECK(!memcmp(guest + 3 * BLOCK - 300, buf_a + 2 * BLOCK - 300, 300));
   TEST_CHECK(!memcmp(guest + 3 * BLOCK, buf_b, 700));
   TEST_CHECK(!memcmp(guest, before, 3 * BLOCK - 300));
   TEST_CHECK(!memcmp(guest + 3 * BLOCK + 700, before + 3 * BLOCK + 700, GUEST - 3 * BLOCK - 700));

   /* The guest's own memory as the buffer: nothing to copy, all well */
   memcpy(before, guest, GUEST);
   TEST_CHECK(rsx_dma_views_sync(5 * BLOCK, BLOCK, 1, guest));
   TEST_CHECK(rsx_dma_views_sync(5 * BLOCK, BLOCK, 0, guest));
   TEST_CHECK(!memcmp(guest, before, GUEST));

   /* Runs into memory no view covers: what could be copied was, and it
    * says so */
   memset(buf_b, 0xEE, sizeof(buf_b));
   TEST_CHECK(!rsx_dma_views_sync(4 * BLOCK - 16, 32, 1, guest));
   TEST_CHECK(!memcmp(buf_b + BLOCK - 16, guest + 4 * BLOCK - 16, 16));

   /* Unset */
   rsx_dma_views_set(a.base, a.end, NULL);
   TEST_CHECK(rsx_dma_views_find(BLOCK) == NULL);
   TEST_CHECK(rsx_dma_views_find(3 * BLOCK) == &b);
   rsx_dma_views_clear();
   TEST_CHECK(rsx_dma_views_find(3 * BLOCK) == NULL);
}

/* One block's view replaced over and over by views of other sizes; the
 * readers check that what they find covers the address */
static retro_atomic_int_t stop;
static retro_atomic_int_t bad;
static struct rsx_dma_view generations[64];

static void *reader(void *arg)
{
   uint32_t hits = 0;
   (void)arg;

   /* Until told to stop, and then once more: after the last change a
    * view is there, so a reader that hardly got to run still checks one */
   for (;;)
   {
      int last = retro_atomic_load_acquire_int(&stop);
      const struct rsx_dma_view *v = rsx_dma_views_find(2 * BLOCK + 123);
      if (v)
      {
         hits++;
         if (v->base > 2 * BLOCK + 123 || v->end < 2 * BLOCK + 123 || v->buffer != v)
            retro_atomic_fetch_add_int(&bad, 1);
      }
      if (last)
         break;
   }

   if (!hits)
      retro_atomic_fetch_add_int(&bad, 1);
   return NULL;
}

static void together(void)
{
   pthread_t r[3];
   uint32_t round, t;

   for (t = 0; t < 64; t++)
   {
      generations[t].base    = (t & 1) ? BLOCK : 2 * BLOCK;
      generations[t].end     = (t & 2) ? 4 * BLOCK - 1 : 3 * BLOCK - 1;
      generations[t].buffer  = &generations[t];
      generations[t].mapping = NULL;
   }

   rsx_dma_views_clear();
   rsx_dma_views_set(generations[0].base, generations[0].end, &generations[0]);
   for (t = 0; t < 3; t++)
      pthread_create(&r[t], NULL, reader, NULL);

   for (round = 0; round < 200000; round++)
   {
      const struct rsx_dma_view *v = &generations[round & 63];
      rsx_dma_views_set(BLOCK, 4 * BLOCK - 1, NULL);
      rsx_dma_views_set(v->base, v->end, v);
   }

   retro_atomic_store_release_int(&stop, 1);
   for (t = 0; t < 3; t++)
      pthread_join(r[t], NULL);

   TEST_CHECK(retro_atomic_load_acquire_int(&bad) == 0);
   rsx_dma_views_clear();
}

int main(void)
{
   alone();
   together();
   printf("test_dma_views: %s\n", test_failures ? "FAIL" : "ok");
   return test_failures ? 1 : 0;
}
