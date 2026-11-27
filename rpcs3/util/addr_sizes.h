#ifndef RPCS3_UTIL_ADDR_SIZES_H
#define RPCS3_UTIL_ADDR_SIZES_H

/* A few 32-bit values by 32-bit address, kept sorted in one array: a PPU
 * function's basic blocks (address -> size) and a module's segments
 * (address -> index), Emu/Cell/PPUAnalyser.h. They were std::map<u32, u32>:
 * a node allocated for each pair, and a module has tens of thousands of
 * functions, most of them with one block.
 *
 * The first two pairs are in the list itself, so most lists allocate
 * nothing; more go to the heap, doubling. A lookup is a search of the
 * array. Adding in the middle moves the pairs after it, and any add may
 * move them all, so a pointer to a pair holds only until the next add:
 * right for lists that stay short, wrong for thousands added in no order.
 * One thread at a time. C89.
 *
 * Plain data: zeroed, a list is empty; freed, it is empty again. It is not
 * to be copied bitwise (it may point into itself): addr_sizes_copy and
 * addr_sizes_move. */

#include <stddef.h>
#include <stdint.h>

#include <retro_inline.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ADDR_SIZES_INLINE 2u

typedef struct addr_size
{
   uint32_t addr;
   uint32_t size;
} addr_size_t;

typedef struct addr_sizes
{
   addr_size_t *heap;    /* cap of them past ADDR_SIZES_INLINE; NULL: in place */
   uint32_t     count;
   uint32_t     cap;     /* on the heap; 0 in place */
   addr_size_t  in_place[ADDR_SIZES_INLINE];
} addr_sizes_t;

/* The pairs, count of them, by rising address */
static INLINE addr_size_t *addr_sizes_data(addr_sizes_t *l)
{
   return l->heap ? l->heap : l->in_place;
}

static INLINE const addr_size_t *addr_sizes_cdata(const addr_sizes_t *l)
{
   return l->heap ? l->heap : l->in_place;
}

/* The index of the first pair at or after @addr; count where there is none */
static INLINE uint32_t addr_sizes_lower(const addr_sizes_t *l, uint32_t addr)
{
   const addr_size_t *e = addr_sizes_cdata(l);
   uint32_t lo = 0, n = l->count;

   /* Halved down to a few, then walked: no branch the search can miss on */
   while (n > 4u)
   {
      const uint32_t half = n / 2u;
      lo += e[lo + half - 1u].addr < addr ? half : 0u;
      n  -= half;
   }
   while (n && e[lo].addr < addr)
   {
      lo++;
      n--;
   }
   return lo;
}

/* The index of the first pair after @addr; count where there is none */
static INLINE uint32_t addr_sizes_upper(const addr_sizes_t *l, uint32_t addr)
{
   return addr == 0xFFFFFFFFu ? l->count : addr_sizes_lower(l, addr + 1u);
}

/* The index of @addr's pair, put in with @size where it was not there
 * (*@added set to whether it was put in; a pair already there keeps its
 * size). 0xFFFFFFFF: out of memory, nothing changed */
uint32_t addr_sizes_add(addr_sizes_t *l, uint32_t addr, uint32_t size, int *added);

/* Emptied; its memory kept */
static INLINE void addr_sizes_clear(addr_sizes_t *l)
{
   l->count = 0;
}

/* @dst (zeroed, or a list) made the same as @src. 0: out of memory, @dst
 * unchanged */
int addr_sizes_copy(addr_sizes_t *dst, const addr_sizes_t *src);

/* @dst (zeroed, or a list) takes what @src has; @src is left empty */
void addr_sizes_move(addr_sizes_t *dst, addr_sizes_t *src);

/* Its memory freed, the list empty */
void addr_sizes_free(addr_sizes_t *l);

#ifdef __cplusplus
}
#endif

#endif
