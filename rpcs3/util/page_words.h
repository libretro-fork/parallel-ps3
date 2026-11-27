#ifndef RPCS3_UTIL_PAGE_WORDS_H
#define RPCS3_UTIL_PAGE_WORDS_H

/* A 32-bit word for each 4 KiB page of a 32-bit address space, found by
 * the address alone: two loads, no hashing, no searching. The words are
 * kept in leaves of 1024 pages (4 MiB of addresses, 4 KiB of words),
 * allocated zeroed when a page of theirs is first asked for and kept
 * until page_words_free. What a word means is its user's; 0 is a page
 * never touched. One thread at a time. C89.
 *
 * Plain data: zeroed, it is empty; freed, it is empty again. */

#include <stddef.h>
#include <stdint.h>

#include <retro_inline.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PAGE_WORDS_LEAVES     1024u
#define PAGE_WORDS_LEAF_PAGES 1024u

typedef struct page_words
{
   uint32_t *leaf[PAGE_WORDS_LEAVES];
} page_words_t;

/* The word of the page @addr is in, or NULL where none of its leaf's
 * pages was ever asked for with page_words_get */
static INLINE uint32_t *page_words_find(const page_words_t *p, uint32_t addr)
{
   uint32_t *leaf = p->leaf[addr >> 22];
   return leaf ? &leaf[(addr >> 12) & (PAGE_WORDS_LEAF_PAGES - 1)] : NULL;
}

/* The same, its leaf allocated where it is not there; NULL when out of
 * memory */
uint32_t *page_words_get(page_words_t *p, uint32_t addr);

/* The address of the page whose word is word @index of leaf @leaf */
static INLINE uint32_t page_words_addr(uint32_t leaf, uint32_t index)
{
   return (leaf << 22) | (index << 12);
}

/* Every leaf freed: empty, and usable again */
void page_words_free(page_words_t *p);

#ifdef __cplusplus
}
#endif

#endif
