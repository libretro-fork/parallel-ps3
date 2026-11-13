/* Which buffer mirrors which part of guest memory, read with no lock.
 *
 * The renderer keeps buffers the GPU can copy to and from that stand for
 * stretches of guest memory (DMA blocks). Every texture upload asks which
 * buffer holds an address, and every transfer through a buffer that is not
 * the guest's memory itself copies between the two. Both only read: this
 * is the reader's side, a table with one entry per 64 KiB of the guest's
 * 4 GiB pointing at the description of the buffer that covers it.
 *
 * A description is made whole and never changed; whoever replaces one
 * keeps the old one alive for as long as a reader may still hold it (the
 * renderer hands it to the same deferred deletion as the buffer). Changing
 * the table is for one thread at a time. */

#ifndef RSX_DMA_VIEWS_H
#define RSX_DMA_VIEWS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct rsx_dma_view
{
   uint32_t base;     /* The first guest address the buffer stands for */
   uint32_t end;      /* and the last */
   void *buffer;      /* The renderer's buffer object */
   uint8_t *mapping;  /* Its memory, or NULL when it is the guest's own:
                         nothing to copy then */
};

/* The entries for the guest addresses first..last (whole 64 KiB blocks:
 * first rounded down, last up) point at view from here on; NULL for none */
void rsx_dma_views_set(uint32_t first, uint32_t last,
      const struct rsx_dma_view *view);

/* No entry points anywhere */
void rsx_dma_views_clear(void);

/* The view over the address, or NULL */
const struct rsx_dma_view *rsx_dma_views_find(uint32_t address);

/* The view that holds all of address..address+length-1, or NULL */
const struct rsx_dma_view *rsx_dma_views_find_range(uint32_t address,
      uint32_t length);

/* Copies address..address+length-1 from guest memory into the buffers
 * that stand for it (load) or back (not load); guest is where guest
 * address 0 is. Views with no mapping are passed over. Returns 0, having
 * copied what came before, when it meets an address no view covers. */
int rsx_dma_views_sync(uint32_t address, uint32_t length, int load,
      uint8_t *guest);

#ifdef __cplusplus
}
#endif

#endif
