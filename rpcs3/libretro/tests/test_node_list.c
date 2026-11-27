/* A doubly linked list with its own nodes (rpcs3/util/node_list.c), where
 * the surface store kept a std::list of the surfaces taken out of use:
 *
 * - added at the end, walked in the order of adding, both ways;
 * - a node stays where it is: its address and its room hold while others
 *   are added (into new blocks too) and erased around it;
 * - erased from the front, the middle and the end, the node after it given
 *   back, the count right, the neighbours joined;
 * - an erased node is used again before a new block is taken, its room
 *   zeroed;
 * - freed: empty, and usable again. */

#include "../../util/node_list.h"
#include "test_common.h"

#include <stdio.h>
#include <string.h>

static void put(node_list_node_t *n, size_t v)
{
   memcpy(n->room.bytes, &v, sizeof(v));
}

static size_t get(const node_list_node_t *n)
{
   size_t v;
   memcpy(&v, n->room.bytes, sizeof(v));
   return v;
}

/* The list is exactly @vals, forward and backward */
static void expect(const node_list_t *l, const size_t *vals, size_t count)
{
   const node_list_node_t *n;
   size_t i = 0;
   TEST_CHECK(l->count == count);
   for (n = l->head; n; n = n->next, i++)
      TEST_CHECK(i < count && get(n) == vals[i]);
   TEST_CHECK(i == count);
   for (n = l->tail; n; n = n->prev)
      TEST_CHECK(i-- > 0 && get(n) == vals[i]);
   TEST_CHECK(i == 0);
   TEST_CHECK(count ? (l->head && !l->head->prev && l->tail && !l->tail->next) : (!l->head && !l->tail));
}

int main(void)
{
   node_list_t l;
   node_list_node_t *nodes[200];
   node_list_node_t *n;
   size_t vals[200];
   size_t i, count;

   memset(&l, 0, sizeof(l));
   memset(vals, 0, sizeof(vals));
   expect(&l, vals, 0);

   /* Added in order; the first block is 32 nodes, so this takes several */
   for (i = 0; i < 100; i++)
   {
      nodes[i] = node_list_push_back(&l);
      TEST_CHECK(nodes[i] != NULL);
      if (!nodes[i])
         return 1;
      TEST_CHECK(get(nodes[i]) == 0);
      put(nodes[i], i + 1);
      vals[i] = i + 1;
   }
   expect(&l, vals, 100);

   /* Nodes stayed where they were through the later adds */
   for (i = 0; i < 100; i++)
      TEST_CHECK(get(nodes[i]) == i + 1);

   /* Erased: the middle, the front, the end; the next node given back */
   n = node_list_erase(&l, nodes[50]);
   TEST_CHECK(n == nodes[51]);
   n = node_list_erase(&l, nodes[0]);
   TEST_CHECK(n == nodes[1] && l.head == nodes[1]);
   n = node_list_erase(&l, nodes[99]);
   TEST_CHECK(n == NULL && l.tail == nodes[98]);
   count = 0;
   for (i = 1; i < 99; i++)
      if (i != 50)
         vals[count++] = i + 1;
   expect(&l, vals, 97);
   /* The others did not move */
   TEST_CHECK(get(nodes[49]) == 50 && get(nodes[51]) == 52 && nodes[49]->next == nodes[51] && nodes[51]->prev == nodes[49]);

   /* The three erased are used again, last erased first, rooms zeroed: no
    * new block until they are gone */
   {
      void *blocks = l.blocks;
      n = node_list_push_back(&l);
      TEST_CHECK(n == nodes[99] && get(n) == 0);
      put(n, 1000);
      vals[count++] = 1000;
      n = node_list_push_back(&l);
      TEST_CHECK(n == nodes[0] && get(n) == 0);
      put(n, 1001);
      vals[count++] = 1001;
      n = node_list_push_back(&l);
      TEST_CHECK(n == nodes[50] && get(n) == 0);
      put(n, 1002);
      vals[count++] = 1002;
      TEST_CHECK(l.blocks == blocks);
      expect(&l, vals, 100);
   }

   /* Emptied node by node from the front, then filled again */
   while (l.head)
      node_list_erase(&l, l.head);
   expect(&l, vals, 0);
   for (i = 0; i < 200; i++)
   {
      n = node_list_push_back(&l);
      TEST_CHECK(n != NULL);
      if (!n)
         return 1;
      put(n, i * 3);
      vals[i] = i * 3;
   }
   expect(&l, vals, 200);

   /* Every second one out while walking, as remove_if does */
   count = 0;
   for (n = l.head, i = 0; n; i++)
   {
      if (i & 1)
         n = node_list_erase(&l, n);
      else
      {
         vals[count++] = get(n);
         n = n->next;
      }
   }
   expect(&l, vals, 100);

   node_list_free(&l);
   expect(&l, vals, 0);
   TEST_CHECK(!l.spare && !l.blocks);
   n = node_list_push_back(&l);
   TEST_CHECK(n != NULL && l.count == 1 && l.head == n && l.tail == n);
   node_list_free(&l);

   if (test_failures)
   {
      fprintf(stderr, "test_node_list: %d failures\n", test_failures);
      return 1;
   }
   printf("test_node_list: ok\n");
   return 0;
}
