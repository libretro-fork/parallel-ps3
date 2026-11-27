/* An object for each of some SPU instruction addresses, in address order
 * (rpcs3/util/pc_slots.c), where the SPU analyser kept std::map<u32, T>:
 *
 * - an address with none is found as none; one that cannot have one (not
 *   a multiple of 4, past the end of local storage) is found as none and
 *   refused by add; the last address, the end of local storage, has one;
 * - an object stays where it is, with what was written to it, while
 *   others are added: also across slabs;
 * - "from" is the first at or after an address and "before" the last
 *   below it, for every kind of address: on one, between two, below the
 *   first, past the last, not a multiple of 4, past local storage;
 * - cleared: none found; the leaves and slabs are used again, so the same
 *   addresses added in the same order get the same memory;
 * - freed: empty and usable, the object size kept;
 * - against a plain model through rounds of random adds and lookups, for
 *   a small object and one larger than a slab's share. */

#include "../../util/pc_slots.h"
#include "test_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SLOTS (PC_SLOTS_LAST / 4u + 1u)

static void *m_obj[SLOTS];

static uint32_t rnd_state = 2463534242u;
static uint32_t rnd(void)
{
   rnd_state = rnd_state * 1664525u + 1013904223u;
   return rnd_state >> 8;
}

/* What the model says is first at or after @pc / last before @pc */
static void *model_from(uint32_t pc)
{
   uint32_t i;
   if (pc > PC_SLOTS_LAST)
      return NULL;
   for (i = (pc + 3u) / 4u; i < SLOTS; i++)
      if (m_obj[i])
         return m_obj[i];
   return NULL;
}

static void *model_before(uint32_t pc)
{
   uint32_t i;
   if (!pc)
      return NULL;
   if (pc > PC_SLOTS_LAST)
      pc = PC_SLOTS_LAST + 4u;
   for (i = (pc - 1u) / 4u + 1u; i--;)
      if (m_obj[i])
         return m_obj[i];
   return NULL;
}

/* Each object starts with its address and is filled with a byte of it */
static void stamp(void *object, uint32_t pc, size_t size)
{
   memset(object, (int)(pc / 4u * 7u + 1u), size);
   memcpy(object, &pc, sizeof(pc));
}

static int stamped(const void *object, uint32_t pc, size_t size)
{
   const unsigned char *p = (const unsigned char*)object;
   uint32_t got;
   size_t i;
   memcpy(&got, object, sizeof(got));
   if (got != pc)
      return 0;
   for (i = sizeof(got); i < size; i++)
      if (p[i] != (unsigned char)(pc / 4u * 7u + 1u))
         return 0;
   return 1;
}

static void compare(const pc_slots_t *t, size_t size)
{
   uint32_t i, live = 0, walked = 0, pc;
   void *e;

   for (i = 0; i < SLOTS; i++)
   {
      live += m_obj[i] != NULL;
      TEST_CHECK(pc_slots_find(t, i * 4u) == m_obj[i]);
      if (m_obj[i])
         TEST_CHECK(stamped(m_obj[i], i * 4u, size));
   }
   TEST_CHECK(t->count == live);

   /* Forwards and backwards: exactly the model's, in order */
   for (e = pc_slots_from(t, 0); e; e = pc_slots_from(t, pc + 4u))
   {
      memcpy(&pc, e, sizeof(pc));
      TEST_CHECK(e == m_obj[pc / 4u]);
      walked++;
   }
   TEST_CHECK(walked == live);

   walked = 0;
   for (e = pc_slots_before(t, 0xffffffffu); e; e = pc_slots_before(t, pc))
   {
      memcpy(&pc, e, sizeof(pc));
      TEST_CHECK(e == m_obj[pc / 4u]);
      walked++;
   }
   TEST_CHECK(walked == live);
}

static void run(size_t size, uint32_t rounds, uint32_t adds, uint32_t span)
{
   pc_slots_t t;
   uint32_t round, i, added;
   void *first_round[64];
   uint32_t first_pcs[64];
   uint32_t first_n = 0;

   memset(&t, 0, sizeof(t));
   pc_slots_init(&t, size);
   TEST_CHECK(t.stride >= size && t.stride % PC_SLOTS_ALIGN == 0 && t.per_slab >= 8u);

   TEST_CHECK(!pc_slots_find(&t, 0) && !pc_slots_from(&t, 0) && !pc_slots_before(&t, 0xffffffffu));
   TEST_CHECK(!pc_slots_add(&t, 2) && !pc_slots_add(&t, PC_SLOTS_LAST + 4u) && !pc_slots_add(&t, 0xfffffffcu));
   TEST_CHECK(t.count == 0);

   for (round = 0; round < rounds; round++)
   {
      /* The same addresses in rounds 0 and 1, to see memory used again */
      if (round < 2u)
         rnd_state = 2463534242u ^ (uint32_t)size;
      added = 0;

      memset(m_obj, 0, sizeof(m_obj));

      for (i = 0; i < adds; i++)
      {
         uint32_t pc = (rnd() % span) * 4u;
         if (i == 0)
            pc = PC_SLOTS_LAST;
         if (m_obj[pc / 4u])
            continue;

         m_obj[pc / 4u] = pc_slots_add(&t, pc);
         TEST_CHECK(m_obj[pc / 4u] != NULL);
         if (!m_obj[pc / 4u])
            return;
         TEST_CHECK(((size_t)m_obj[pc / 4u] & (PC_SLOTS_ALIGN - 1u)) == 0);
         stamp(m_obj[pc / 4u], pc, size);

         if (round == 0 && first_n < 64u)
         {
            first_round[first_n] = m_obj[pc / 4u];
            first_pcs[first_n++] = pc;
         }
         else if (round == 1 && added < first_n)
            TEST_CHECK(first_pcs[added] == pc && first_round[added] == m_obj[pc / 4u]);
         added++;

         /* Lookups of every kind against the model */
         {
            uint32_t q = rnd() % (PC_SLOTS_LAST + 64u);
            TEST_CHECK(pc_slots_from(&t, q) == model_from(q));
            TEST_CHECK(pc_slots_before(&t, q) == model_before(q));
            TEST_CHECK(pc_slots_from(&t, pc) == m_obj[pc / 4u]);
            TEST_CHECK(pc_slots_from(&t, pc + 1u) == model_from(pc + 1u));
            TEST_CHECK(pc_slots_before(&t, pc + 4u) == m_obj[pc / 4u]);
            TEST_CHECK(pc_slots_before(&t, pc) == model_before(pc));
            if (pc & 4u)
               TEST_CHECK(!pc_slots_find(&t, pc + 1u) && !pc_slots_find(&t, pc + 2u));
         }
      }

      TEST_CHECK(pc_slots_from(&t, PC_SLOTS_LAST) == m_obj[PC_SLOTS_LAST / 4u]);
      TEST_CHECK(!pc_slots_from(&t, PC_SLOTS_LAST + 1u) && !pc_slots_from(&t, 0xffffffffu));
      TEST_CHECK(pc_slots_before(&t, 0xffffffffu) == m_obj[PC_SLOTS_LAST / 4u]);
      TEST_CHECK(!pc_slots_before(&t, 0));

      compare(&t, size);

      if (round == rounds / 2u)
      {
         pc_slots_free(&t);
         TEST_CHECK(t.stride >= size && !t.first && !t.count);
      }
      else
         pc_slots_clear(&t);

      TEST_CHECK(t.count == 0 && !pc_slots_from(&t, 0) && !pc_slots_before(&t, 0xffffffffu) && !pc_slots_find(&t, PC_SLOTS_LAST));
   }

   pc_slots_free(&t);
}

int main(void)
{
   run(8, 6, 3000, SLOTS);        /* small: many to a slab, all over local storage */
   run(40, 6, 20000, 4096);       /* dense in a few leaves */
   run(2600, 8, 400, SLOTS);      /* about a basic block's size: many slabs */
   run(40000, 4, 40, 2000);       /* larger than a slab's share: the minimum of 8 */

   if (test_failures)
   {
      fprintf(stderr, "test_pc_slots: %d check(s) failed\n", test_failures);
      return 1;
   }
   printf("test_pc_slots: ok\n");
   return 0;
}
