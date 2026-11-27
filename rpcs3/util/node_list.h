#ifndef RPCS3_UTIL_NODE_LIST_H
#define RPCS3_UTIL_NODE_LIST_H

/* A doubly linked list whose nodes come from the list's own blocks, where
 * a std::list allocated one for every element and freed it on removal.
 *
 * It keeps what the std::list gave its users: a node stays where it is
 * until it is erased - adding or erasing others moves nothing, so a
 * pointer to a node or into its room holds across them - and the order
 * is the order of adding. An erased node is used again by a later add;
 * blocks go back only with node_list_free. One thread at a time. C89.
 *
 * Each node has room for one pointer-sized value that may be moved as
 * plain bytes (a pointer, a std::unique_ptr with the default deleter):
 * the list never looks at it. */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct node_list_node
{
   struct node_list_node *prev;
   struct node_list_node *next;
   union
   {
      void *align;
      unsigned char bytes[sizeof(void*)];
   } room;
} node_list_node_t;

typedef struct node_list
{
   node_list_node_t *head;
   node_list_node_t *tail;
   node_list_node_t *spare;   /* erased nodes and those of a new block, by next */
   void *blocks;              /* every block allocated, for node_list_free */
   size_t count;
} node_list_t;

/* A node added at the end, its room zeroed; NULL when out of memory */
node_list_node_t *node_list_push_back(node_list_t *l);

/* @n out of the list, kept for a later add. Returns the node that
 * followed it (NULL at the end). */
node_list_node_t *node_list_erase(node_list_t *l, node_list_node_t *n);

/* Every node and block freed, the list empty and usable again. Rooms are
 * not looked at: what they hold is the caller's to release first. */
void node_list_free(node_list_t *l);

#ifdef __cplusplus
}
#endif

#endif
