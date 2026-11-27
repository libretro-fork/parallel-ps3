/* A doubly linked list with its own nodes: see node_list.h. C89. */

#include "node_list.h"

#include <stdlib.h>
#include <string.h>

#define NODE_LIST_BLOCK 32

typedef struct node_list_block
{
   struct node_list_block *next;
   node_list_node_t nodes[NODE_LIST_BLOCK];
} node_list_block_t;

node_list_node_t *node_list_push_back(node_list_t *l)
{
   node_list_node_t *n = l->spare;

   if (!n)
   {
      node_list_block_t *b = (node_list_block_t*)malloc(sizeof(*b));
      size_t i;
      if (!b)
         return NULL;
      b->next = (node_list_block_t*)l->blocks;
      l->blocks = b;
      for (i = 0; i + 1 < NODE_LIST_BLOCK; i++)
         b->nodes[i].next = &b->nodes[i + 1];
      b->nodes[NODE_LIST_BLOCK - 1].next = NULL;
      n = &b->nodes[0];
   }

   l->spare = n->next;
   memset(&n->room, 0, sizeof(n->room));
   n->next = NULL;
   n->prev = l->tail;
   if (l->tail)
      l->tail->next = n;
   else
      l->head = n;
   l->tail = n;
   l->count++;
   return n;
}

node_list_node_t *node_list_erase(node_list_t *l, node_list_node_t *n)
{
   node_list_node_t *next = n->next;

   if (n->prev)
      n->prev->next = next;
   else
      l->head = next;
   if (next)
      next->prev = n->prev;
   else
      l->tail = n->prev;

   n->prev = NULL;
   n->next = l->spare;
   l->spare = n;
   l->count--;
   return next;
}

void node_list_free(node_list_t *l)
{
   node_list_block_t *b = (node_list_block_t*)l->blocks;
   while (b)
   {
      node_list_block_t *next = b->next;
      free(b);
      b = next;
   }
   memset(l, 0, sizeof(*l));
}
