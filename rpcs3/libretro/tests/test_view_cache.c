/* A frame's image views by address and properties
 * (rpcs3/util/view_cache.c), where the texture cache kept a
 * std::unordered_multimap of whole requests:
 *
 * - found by address and properties together: the same address with other
 *   properties, and the same properties at another address, are apart;
 *   one not there is found as none, in an empty cache too;
 * - what was kept with a view comes back with it;
 * - many kept: all found after the table grows and grows again;
 * - taken out: found as none, the others (those probing past it too)
 *   still found, the same one kept again found again;
 * - a table worn down by taking out and keeping is rebuilt without
 *   growing, and loses none;
 * - emptied: nothing found, the memory kept; freed: empty and usable. */

#include "../../util/view_cache.h"
#include "test_common.h"

#include <stdio.h>
#include <string.h>

static int views[4096];

int main(void)
{
   view_cache_t c;
   view_cache_entry_t *e;
   uint32_t i, cap, live;

   memset(&c, 0, sizeof(c));
   TEST_CHECK(view_cache_find(&c, 0x1000, 7) == NULL);
   TEST_CHECK(view_cache_add(&c, 0x1000, 7, 3, 0x1000, 0x1fff, NULL) == 0);
   TEST_CHECK(view_cache_add(&c, 0x1000, 7, 3, 0x1000, 0x1fff, VIEW_CACHE_DEAD) == 0 && c.live == 0);

   TEST_CHECK(view_cache_add(&c, 0x1000, 7, 3, 0x1000, 0x1fff, &views[0]) == 1);
   TEST_CHECK(view_cache_add(&c, 0x1000, 8, 4, 0x1000, 0x2fff, &views[1]) == 1);
   TEST_CHECK(view_cache_add(&c, 0x2000, 7, 5, 0x2000, 0x2fff, &views[2]) == 1);
   TEST_CHECK(c.live == 3 && c.used == 3);

   e = view_cache_find(&c, 0x1000, 7);
   TEST_CHECK(e && e->view == &views[0] && e->remap == 3 && e->range_start == 0x1000 && e->range_end == 0x1fff);
   e = view_cache_find(&c, 0x1000, 8);
   TEST_CHECK(e && e->view == &views[1] && e->remap == 4 && e->range_end == 0x2fff);
   e = view_cache_find(&c, 0x2000, 7);
   TEST_CHECK(e && e->view == &views[2] && e->remap == 5);
   TEST_CHECK(view_cache_find(&c, 0x2000, 8) == NULL && view_cache_find(&c, 0x3000, 7) == NULL);

   /* Many: the table grows; all still found, with what they were kept with */
   for (i = 3; i < 2000; i++)
      TEST_CHECK(view_cache_add(&c, 0x100000 + (i / 4) * 0x1000, ((uint64_t)(i & 3) << 56) | i, i, i, i + 9, &views[i]) == 1);
   TEST_CHECK(c.live == 2000 && c.cap >= 4000 && (c.cap & (c.cap - 1)) == 0);
   for (i = 3; i < 2000; i++)
   {
      e = view_cache_find(&c, 0x100000 + (i / 4) * 0x1000, ((uint64_t)(i & 3) << 56) | i);
      TEST_CHECK(e && e->view == &views[i] && e->remap == i && e->range_start == i && e->range_end == i + 9);
   }
   TEST_CHECK(view_cache_find(&c, 0x1000, 7) != NULL);

   /* Every third one taken out; the rest found, those not */
   for (i = 3; i < 2000; i += 3)
   {
      e = view_cache_find(&c, 0x100000 + (i / 4) * 0x1000, ((uint64_t)(i & 3) << 56) | i);
      TEST_CHECK(e != NULL);
      if (e)
         view_cache_kill(&c, e);
   }
   live = 0;
   for (i = 3; i < 2000; i++)
   {
      e = view_cache_find(&c, 0x100000 + (i / 4) * 0x1000, ((uint64_t)(i & 3) << 56) | i);
      if (i % 3 == 0)
         TEST_CHECK(e == NULL);
      else
      {
         TEST_CHECK(e && e->view == &views[i]);
         live++;
      }
   }
   TEST_CHECK(c.live == live + 3);
   /* Going through it sees exactly the live ones */
   live = 0;
   for (i = 0; i < c.cap; i++)
      live += VIEW_CACHE_LIVE(&c.e[i]) ? 1 : 0;
   TEST_CHECK(live == c.live);

   /* One taken out and kept again: found, with the new view */
   TEST_CHECK(view_cache_add(&c, 0x100000, ((uint64_t)3 << 56) | 3, 77, 1, 2, &views[3000]) == 1);
   e = view_cache_find(&c, 0x100000, ((uint64_t)3 << 56) | 3);
   TEST_CHECK(e && e->view == &views[3000] && e->remap == 77);

   /* Kept and taken out over and over: dead entries are cleared away
    * without the table growing, and no live one is lost */
   cap = c.cap;
   live = c.live;
   for (i = 0; i < 20000; i++)
   {
      TEST_CHECK(view_cache_add(&c, 0x9000000 + i, 1, 0, 0, 0, &views[3001]) == 1);
      e = view_cache_find(&c, 0x9000000 + i, 1);
      TEST_CHECK(e != NULL);
      if (e)
         view_cache_kill(&c, e);
   }
   TEST_CHECK(c.cap == cap && c.live == live);
   for (i = 4; i < 2000; i++)
      if (i % 3)
         TEST_CHECK(view_cache_find(&c, 0x100000 + (i / 4) * 0x1000, ((uint64_t)(i & 3) << 56) | i) != NULL);

   /* The end of the frame: empty, the memory kept */
   e = c.e;
   view_cache_clear(&c);
   TEST_CHECK(c.live == 0 && c.used == 0 && c.cap == cap && c.e == e);
   TEST_CHECK(view_cache_find(&c, 0x1000, 7) == NULL && view_cache_find(&c, 0x100000 + 1 * 0x1000, ((uint64_t)1 << 56) | 5) == NULL);
   TEST_CHECK(view_cache_add(&c, 0x1000, 7, 3, 0x1000, 0x1fff, &views[0]) == 1);
   TEST_CHECK(view_cache_find(&c, 0x1000, 7) != NULL && c.live == 1);

   view_cache_free(&c);
   TEST_CHECK(c.e == NULL && c.cap == 0 && c.live == 0);
   TEST_CHECK(view_cache_find(&c, 0x1000, 7) == NULL);
   TEST_CHECK(view_cache_add(&c, 0x1000, 7, 3, 0x1000, 0x1fff, &views[0]) == 1 && view_cache_find(&c, 0x1000, 7) != NULL);
   view_cache_free(&c);

   if (test_failures)
   {
      fprintf(stderr, "test_view_cache: %d failures\n", test_failures);
      return 1;
   }
   printf("test_view_cache: ok\n");
   return 0;
}
