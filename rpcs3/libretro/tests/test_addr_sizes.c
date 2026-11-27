/* A few values by address, sorted in one array (rpcs3/util/addr_sizes.c),
 * where a PPU function's blocks and a module's segments were
 * std::map<u32, u32>:
 *
 * - empty: nothing found, lower and upper are 0;
 * - the first two pairs are in the list itself (no allocation), the third
 *   moves them to the heap, all still there and in order;
 * - added again, an address keeps its value and is reported as not added;
 * - lower is the first at or after an address, upper the first after it:
 *   for addresses on a pair, between two, below all, above all, 0 and
 *   0xFFFFFFFF, both of which can be keys;
 * - a copy is the same and its own (also of an in-place list, also onto a
 *   list that had more); a move leaves the source empty and usable;
 * - cleared keeps its memory; freed is empty and usable;
 * - against a plain model through 200,000 random steps over lists of up
 *   to a few hundred pairs. */

#include "../../util/addr_sizes.h"
#include "test_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MODEL_MAX 512

static uint32_t m_addr[MODEL_MAX], m_size[MODEL_MAX];
static uint32_t m_count;

static uint32_t rnd_state = 88172645u;
static uint32_t rnd(void)
{
   rnd_state ^= rnd_state << 13;
   rnd_state ^= rnd_state >> 17;
   rnd_state ^= rnd_state << 5;
   return rnd_state;
}

static uint32_t model_lower(uint32_t addr)
{
   uint32_t i;
   for (i = 0; i < m_count && m_addr[i] < addr; i++);
   return i;
}

static void model_add(uint32_t addr, uint32_t size, int *added)
{
   const uint32_t at = model_lower(addr);
   if (at < m_count && m_addr[at] == addr)
   {
      *added = 0;
      return;
   }
   memmove(m_addr + at + 1, m_addr + at, (m_count - at) * sizeof(uint32_t));
   memmove(m_size + at + 1, m_size + at, (m_count - at) * sizeof(uint32_t));
   m_addr[at] = addr;
   m_size[at] = size;
   m_count++;
   *added = 1;
}

static void compare(const addr_sizes_t *l)
{
   const addr_size_t *e = addr_sizes_cdata(l);
   uint32_t i;
   TEST_CHECK(l->count == m_count);
   for (i = 0; i < m_count && i < l->count; i++)
      TEST_CHECK(e[i].addr == m_addr[i] && e[i].size == m_size[i]);
}

/* An address that is often one already there, or next to one */
static uint32_t pick(uint32_t span)
{
   const uint32_t r = rnd();
   if (m_count && (r & 3u) == 0)
      return m_addr[(r >> 8) % m_count] + ((r >> 4) & 3u) - 1u;
   if ((r & 63u) == 1)
      return (r & 64u) ? 0u : 0xFFFFFFFFu;
   return (r >> 8) % span * 4u;
}

int main(void)
{
   addr_sizes_t l, c, d;
   int added = -1;
   uint32_t step;

   memset(&l, 0, sizeof(l));
   memset(&c, 0, sizeof(c));
   memset(&d, 0, sizeof(d));

   TEST_CHECK(addr_sizes_lower(&l, 0) == 0 && addr_sizes_upper(&l, 0) == 0 && addr_sizes_upper(&l, 0xFFFFFFFFu) == 0);

   /* Two in place, in either order */
   TEST_CHECK(addr_sizes_add(&l, 0x200, 8, &added) == 0 && added == 1);
   TEST_CHECK(addr_sizes_add(&l, 0x100, 4, &added) == 0 && added == 1);
   TEST_CHECK(!l.heap && l.count == 2 && addr_sizes_data(&l) == l.in_place);
   TEST_CHECK(l.in_place[0].addr == 0x100 && l.in_place[0].size == 4 && l.in_place[1].addr == 0x200 && l.in_place[1].size == 8);
   TEST_CHECK(addr_sizes_add(&l, 0x200, 99, &added) == 1 && added == 0 && l.in_place[1].size == 8);

   TEST_CHECK(addr_sizes_lower(&l, 0) == 0 && addr_sizes_lower(&l, 0x100) == 0 && addr_sizes_lower(&l, 0x101) == 1);
   TEST_CHECK(addr_sizes_lower(&l, 0x200) == 1 && addr_sizes_lower(&l, 0x201) == 2 && addr_sizes_lower(&l, 0xFFFFFFFFu) == 2);
   TEST_CHECK(addr_sizes_upper(&l, 0) == 0 && addr_sizes_upper(&l, 0xFF) == 0 && addr_sizes_upper(&l, 0x100) == 1);
   TEST_CHECK(addr_sizes_upper(&l, 0x1FF) == 1 && addr_sizes_upper(&l, 0x200) == 2 && addr_sizes_upper(&l, 0xFFFFFFFFu) == 2);

   /* A copy of an in-place list is its own */
   TEST_CHECK(addr_sizes_copy(&c, &l) && !c.heap && c.count == 2 && c.in_place[1].addr == 0x200);
   c.in_place[1].size = 7;
   TEST_CHECK(l.in_place[1].size == 8);

   /* The third goes to the heap with the others */
   TEST_CHECK(addr_sizes_add(&l, 0x180, 6, &added) == 1 && added == 1 && l.heap && l.count == 3);
   TEST_CHECK(l.heap[0].addr == 0x100 && l.heap[1].addr == 0x180 && l.heap[1].size == 6 && l.heap[2].addr == 0x200 && l.heap[2].size == 8);

   /* Moved: the source empty and usable, the target has it */
   addr_sizes_move(&d, &l);
   TEST_CHECK(l.count == 0 && !l.heap && d.count == 3 && d.heap && d.heap[2].addr == 0x200);
   TEST_CHECK(addr_sizes_add(&l, 5, 1, &added) == 0 && added == 1 && !l.heap);
   /* An in-place list moved brings its pairs */
   addr_sizes_move(&d, &l);
   TEST_CHECK(d.count == 1 && !d.heap && d.in_place[0].addr == 5 && l.count == 0);
   /* A copy onto a list that had more */
   TEST_CHECK(addr_sizes_copy(&c, &d) && c.count == 1 && c.in_place[0].addr == 5);
   addr_sizes_free(&c);
   addr_sizes_free(&d);
   addr_sizes_free(&l);
   TEST_CHECK(l.count == 0 && !l.heap);

   /* Against the model */
   for (step = 0; step < 200000u; step++)
   {
      const uint32_t r = rnd() % 100u;
      const uint32_t span = (step / 5000u) % 2u ? 64u : 100000u;

      if (r < 60u && m_count < MODEL_MAX)
      {
         const uint32_t addr = pick(span), size = rnd();
         int m_added = -1;
         uint32_t at;
         added = -1;
         model_add(addr, size, &m_added);
         at = addr_sizes_add(&l, addr, size, &added);
         TEST_CHECK(added == m_added && at == model_lower(addr));
         TEST_CHECK(at < l.count && addr_sizes_data(&l)[at].addr == addr);
      }
      else if (r < 90u)
      {
         const uint32_t addr = pick(span);
         const uint32_t lower = model_lower(addr);
         TEST_CHECK(addr_sizes_lower(&l, addr) == lower);
         TEST_CHECK(addr_sizes_upper(&l, addr) == lower + (lower < m_count && m_addr[lower] == addr ? 1u : 0u));
      }
      else if (r < 93u)
      {
         compare(&l);
         TEST_CHECK(addr_sizes_copy(&c, &l));
         compare(&c);
         if (rnd() & 1u)
         {
            /* Carry on with the copy, or with a moved one */
            if (rnd() & 1u)
            {
               addr_sizes_move(&d, &c);
               addr_sizes_move(&l, &d);
            }
            else
            {
               addr_sizes_free(&l);
               addr_sizes_move(&l, &c);
            }
            compare(&l);
         }
      }
      else if (r < 95u || m_count == MODEL_MAX)
      {
         compare(&l);
         if (rnd() & 1u)
            addr_sizes_clear(&l);
         else
            addr_sizes_free(&l);
         m_count = 0;
         TEST_CHECK(l.count == 0 && addr_sizes_lower(&l, 0) == 0);
      }
   }
   compare(&l);

   addr_sizes_free(&l);
   addr_sizes_free(&c);
   addr_sizes_free(&d);

   if (test_failures)
   {
      fprintf(stderr, "test_addr_sizes: %d check(s) failed\n", test_failures);
      return 1;
   }
   printf("test_addr_sizes: ok\n");
   return 0;
}
