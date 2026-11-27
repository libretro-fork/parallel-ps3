/* A list of values for each SPU instruction address
 * (rpcs3/util/pc_lists.c), where the SPU analyser kept
 * std::unordered_map<u32, std::vector<u32>>:
 *
 * - an address not there is found as none; one that cannot be there (not a
 *   multiple of 4, past the end of local storage) is found as none and
 *   refused by get; the last address, the end of local storage, is one;
 * - got: there, with an empty list; got again: the same entry, its list
 *   kept;
 * - an entry and its list's values stay where they are while other
 *   addresses are got and erased around them;
 * - erased: not found, the count down; got again: empty, its memory used
 *   again;
 * - gone through from 0 by pc + 4: exactly the live ones, by rising
 *   address, also across leaves that are there but empty;
 * - cleared: none found, the leaves and lists' memory kept; freed: empty
 *   and usable;
 * - against a plain model through 300,000 random steps. */

#include "../../util/pc_lists.h"
#include "test_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SLOTS (PC_LISTS_LAST / 4u + 1u)
#define MODEL_MAX 24u

static unsigned char m_live[SLOTS];
static unsigned char m_count[SLOTS];
static uint32_t m_vals[SLOTS][MODEL_MAX];

static uint32_t rnd_state = 12345u;
static uint32_t rnd(void)
{
   rnd_state = rnd_state * 1664525u + 1013904223u;
   return rnd_state >> 8;
}

/* The table is exactly the model */
static void compare(const pc_lists_t *t)
{
   const pc_list_entry_t *e;
   uint32_t pc = 0, live = 0, i, j;

   for (i = 0; i < SLOTS; i++)
      live += m_live[i];
   TEST_CHECK(t->count == live);

   i = 0;
   for (e = pc_lists_from(t, 0); e; e = pc_lists_from(t, e->pc + 4))
   {
      TEST_CHECK(e->pc >= pc && pc_lists_valid(e->pc));
      /* None live in the model between the last one and this */
      for (j = pc / 4; j < e->pc / 4; j++)
         TEST_CHECK(!m_live[j]);
      TEST_CHECK(m_live[e->pc / 4] && e->list.count == m_count[e->pc / 4]);
      if (e->list.count == m_count[e->pc / 4] && e->list.count)
         TEST_CHECK(memcmp(e->list.data, m_vals[e->pc / 4], e->list.count * sizeof(uint32_t)) == 0);
      pc = e->pc + 4;
      i++;
   }
   for (j = pc / 4; j < SLOTS; j++)
      TEST_CHECK(!m_live[j]);
   TEST_CHECK(i == live);
}

int main(void)
{
   static pc_lists_t t;
   pc_list_entry_t *a, *b, *c;
   uint32_t *data;
   uint32_t i, step;

   memset(&t, 0, sizeof(t));
   TEST_CHECK(pc_lists_find(&t, 0x100) == NULL && pc_lists_from(&t, 0) == NULL);
   TEST_CHECK(pc_lists_find(&t, 0x102) == NULL && pc_lists_get(&t, 0x102) == NULL);
   TEST_CHECK(pc_lists_find(&t, PC_LISTS_LAST + 4) == NULL && pc_lists_get(&t, PC_LISTS_LAST + 4) == NULL);
   TEST_CHECK(pc_lists_find(&t, 0xFFFFFFFCu) == NULL && pc_lists_get(&t, 0xFFFFFFFCu) == NULL && t.count == 0);
   TEST_CHECK(pc_lists_from(&t, 0xFFFFFFFCu) == NULL);

   a = pc_lists_get(&t, 0x100);
   TEST_CHECK(a && a->pc == 0x100 && a->live && a->list.count == 0 && t.count == 1);
   if (!a)
      return 1;
   TEST_CHECK(pc_lists_find(&t, 0x100) == a && pc_lists_get(&t, 0x100) == a && t.count == 1);
   for (i = 0; i < 40; i++)
      TEST_CHECK(pc_list_push(&a->list, i * 7) == 1);
   TEST_CHECK(a->list.count == 40 && a->list.cap >= 40);
   data = a->list.data;

   /* Others come and go: the first stays, its values too */
   b = pc_lists_get(&t, 0x104);
   c = pc_lists_get(&t, PC_LISTS_LAST);
   TEST_CHECK(b && c && b == a + 1 && c->pc == PC_LISTS_LAST && t.count == 3);
   if (!b || !c)
      return 1;
   for (i = 0; i < 2000; i++)
   {
      pc_list_entry_t *e = pc_lists_get(&t, 0x8000 + i * 16);
      TEST_CHECK(e != NULL);
      if (e)
         TEST_CHECK(pc_list_push(&e->list, i) == 1);
   }
   for (i = 0; i < 2000; i += 2)
      pc_lists_erase(&t, pc_lists_find(&t, 0x8000 + i * 16));
   TEST_CHECK(pc_lists_find(&t, 0x100) == a && a->list.data == data && a->list.count == 40);
   for (i = 0; i < 40; i++)
      TEST_CHECK(a->list.data[i] == i * 7);
   TEST_CHECK(t.count == 3 + 1000);

   /* Erased, then got again: empty, the list's memory used again */
   pc_lists_erase(&t, a);
   TEST_CHECK(pc_lists_find(&t, 0x100) == NULL && t.count == 3 + 999);
   pc_lists_erase(&t, a); /* twice is once */
   TEST_CHECK(t.count == 3 + 999);
   TEST_CHECK(pc_lists_get(&t, 0x100) == a && a->list.count == 0 && a->list.data == data && a->list.cap >= 40);

   /* Gone through: the first is 0x100, the last the end of local storage */
   TEST_CHECK(pc_lists_from(&t, 0) == a && pc_lists_from(&t, 0x100) == a && pc_lists_from(&t, 0x101) == b && pc_lists_from(&t, 0x104) == b);
   TEST_CHECK(pc_lists_from(&t, 0x108) == pc_lists_find(&t, 0x8000 + 16));
   TEST_CHECK(pc_lists_from(&t, PC_LISTS_LAST) == c && pc_lists_from(&t, PC_LISTS_LAST + 1) == NULL);

   /* Cleared: nothing there, nothing freed */
   pc_lists_clear(&t);
   TEST_CHECK(t.count == 0 && pc_lists_from(&t, 0) == NULL && pc_lists_find(&t, 0x100) == NULL);
   TEST_CHECK(t.leaf[0] != NULL && a->list.data == data);
   TEST_CHECK(pc_lists_get(&t, 0x100) == a && a->list.count == 0);
   pc_lists_clear(&t);

   /* Against the model */
   for (step = 0; step < 300000; step++)
   {
      /* Addresses bunched in a few places, as a program's are, and the ends */
      const uint32_t r = rnd();
      const uint32_t region = (r >> 4) % 5;
      const uint32_t slot = region == 0 ? (r >> 8) % 64
         : region == 1 ? SLOTS - 1 - (r >> 8) % 64
         : region == 2 ? 20000 + (r >> 8) % 700
         : region == 3 ? 255 + (r >> 8) % 4
         : (r >> 8) % SLOTS;
      const uint32_t pc = slot * 4;
      pc_list_entry_t *e;

      switch (r % 8)
      {
      case 0: case 1: case 2:
         e = pc_lists_get(&t, pc);
         TEST_CHECK(e != NULL);
         if (!e)
            return 1;
         if (!m_live[slot])
         {
            m_live[slot] = 1;
            m_count[slot] = 0;
         }
         if (m_count[slot] < MODEL_MAX)
         {
            TEST_CHECK(pc_list_push(&e->list, r) == 1);
            m_vals[slot][m_count[slot]++] = r;
         }
         break;
      case 3:
         e = pc_lists_get(&t, pc); /* there, with whatever it had */
         TEST_CHECK(e != NULL);
         if (!m_live[slot])
         {
            m_live[slot] = 1;
            m_count[slot] = 0;
         }
         break;
      case 4: case 5:
         e = pc_lists_find(&t, pc);
         TEST_CHECK((e != NULL) == (m_live[slot] != 0));
         if (e)
            pc_lists_erase(&t, e);
         m_live[slot] = 0;
         m_count[slot] = 0;
         break;
      case 6:
         e = pc_lists_find(&t, pc);
         TEST_CHECK((e != NULL) == (m_live[slot] != 0));
         if (e)
            TEST_CHECK(e->pc == pc && e->list.count == m_count[slot]);
         break;
      default:
         if ((r >> 12) % 4000 == 0)
         {
            pc_lists_clear(&t);
            memset(m_live, 0, sizeof(m_live));
            memset(m_count, 0, sizeof(m_count));
         }
         break;
      }

      if (step % 5000 == 0)
         compare(&t);
   }
   compare(&t);

   pc_lists_free(&t);
   TEST_CHECK(t.count == 0 && t.leaf[0] == NULL && pc_lists_find(&t, 0x100) == NULL);
   a = pc_lists_get(&t, 0x100);
   TEST_CHECK(a && a->list.count == 0 && a->list.data == NULL);
   pc_lists_free(&t);

   if (test_failures)
   {
      fprintf(stderr, "test_pc_lists: %d failures\n", test_failures);
      return 1;
   }
   printf("test_pc_lists: ok\n");
   return 0;
}
