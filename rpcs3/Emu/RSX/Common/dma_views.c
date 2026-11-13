/* See dma_views.h */

#include "dma_views.h"

#include <string.h>

#include <retro_atomic.h>

#define BLOCK_SHIFT 16
#define BLOCK_COUNT (1u << (32 - BLOCK_SHIFT))

static retro_atomic_ptr_t views[BLOCK_COUNT];

void rsx_dma_views_set(uint32_t first, uint32_t last,
      const struct rsx_dma_view *view)
{
   uint32_t i;
   uint32_t end = last >> BLOCK_SHIFT;

   for (i = first >> BLOCK_SHIFT; i <= end; i++)
      retro_atomic_store_release_ptr(&views[i], (void*)view);
}

void rsx_dma_views_clear(void)
{
   uint32_t i;

   for (i = 0; i < BLOCK_COUNT; i++)
   {
      if (retro_atomic_load_relaxed_ptr(&views[i]))
         retro_atomic_store_release_ptr(&views[i], NULL);
   }
}

const struct rsx_dma_view *rsx_dma_views_find(uint32_t address)
{
   return (const struct rsx_dma_view*)retro_atomic_load_acquire_ptr(
         &views[address >> BLOCK_SHIFT]);
}

const struct rsx_dma_view *rsx_dma_views_find_range(uint32_t address,
      uint32_t length)
{
   const struct rsx_dma_view *view = rsx_dma_views_find(address);
   uint32_t last = address + (length ? length - 1 : 0);

   if (!view || last < address || address < view->base || last > view->end)
      return NULL;

   return view;
}

int rsx_dma_views_sync(uint32_t address, uint32_t length, int load,
      uint8_t *guest)
{
   uint32_t limit;

   if (!length)
      return 1;

   limit = address + length - 1;

   for (;;)
   {
      const struct rsx_dma_view *view = rsx_dma_views_find(address);
      uint32_t last, count;

      if (!view || address < view->base || address > view->end)
         return 0;

      last  = limit < view->end ? limit : view->end;
      count = last - address + 1;

      if (view->mapping)
      {
         uint8_t *in_buffer = view->mapping + (address - view->base);

         if (load)
            memcpy(in_buffer, guest + address, count);
         else
            memcpy(guest + address, in_buffer, count);
      }

      if (last == limit)
         return 1;

      /* The range goes on in the next buffer */
      address = last + 1;
   }
}
