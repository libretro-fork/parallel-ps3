/* The libretro swapchain's images (swapchain_LIBRETRO), as the renderer's
 * thread and retro_run share them, on retro_atomics and without a lock.
 * C89, so the test drives the same code under ThreadSanitizer.
 *
 * - Which images are held: an image is taken by the flip that draws into it
 *   (the renderer's thread) and given back when the frontend is done with
 *   it (retro_run), or when the frame is overtaken.
 * - Image sets retired by a resize or a restart: the renderer's thread
 *   pushes them, retro_run frees them once the frontend is past them. A
 *   stack: pushes by compare-and-swap, taken whole by an exchange. */

#ifndef RPCS3_VK_LIBRETRO_IMAGES_H
#define RPCS3_VK_LIBRETRO_IMAGES_H

#include <stddef.h>

#include <retro_atomic.h>
#include <retro_inline.h>

#define LR_MAX_IMAGES 8

/* A C cast in C; in C++ (RPCS3 builds with -Werror=old-style-cast) a C++ one */
#ifdef __cplusplus
#define LR_CAST(type, value) static_cast<type>(value)
#else
#define LR_CAST(type, value) ((type)(value))
#endif

struct lr_image_slots
{
   retro_atomic_int_t held[LR_MAX_IMAGES];
   unsigned count;
};

static INLINE void lr_image_slots_init(struct lr_image_slots *s, unsigned count)
{
   unsigned i;
   s->count = count < LR_MAX_IMAGES ? count : LR_MAX_IMAGES;
   for (i = 0; i < LR_MAX_IMAGES; i++)
      retro_atomic_int_init(&s->held[i], 0);
}

/* A free image, now held; -1 when every one is held */
static INLINE int lr_image_slots_acquire(struct lr_image_slots *s)
{
   unsigned i;
   for (i = 0; i < s->count; i++)
   {
      if (!retro_atomic_load_acquire_int(&s->held[i])
            && retro_atomic_cas_int(&s->held[i], 0, 1))
         return LR_CAST(int, i);
   }
   return -1;
}

static INLINE void lr_image_slots_release(struct lr_image_slots *s, unsigned index)
{
   if (index < s->count && index < LR_MAX_IMAGES)
      retro_atomic_store_release_int(&s->held[index], 0);
}

static INLINE void lr_image_slots_release_all(struct lr_image_slots *s)
{
   unsigned i;
   for (i = 0; i < s->count; i++)
      retro_atomic_store_release_int(&s->held[i], 0);
}

static INLINE int lr_image_slots_held(struct lr_image_slots *s, unsigned index)
{
   return index < s->count && index < LR_MAX_IMAGES
      && retro_atomic_load_acquire_int(&s->held[index]);
}

/* A retired set: @free_cb(@owner) frees it */
struct lr_retired
{
   struct lr_retired *next;
   void (*free_cb)(void *owner);
   void *owner;
};

static INLINE void lr_retired_push(retro_atomic_ptr_t *head, struct lr_retired *node)
{
   for (;;)
   {
      void *top = retro_atomic_load_acquire_ptr(head);
      node->next = LR_CAST(struct lr_retired*, top);
      if (retro_atomic_cas_ptr(head, top, node))
         return;
   }
}

static INLINE int lr_retired_any(retro_atomic_ptr_t *head)
{
   return retro_atomic_load_acquire_ptr(head) != NULL;
}

/* Every set retired so far, freed; the consumer's (one thread) */
static INLINE void lr_retired_free_all(retro_atomic_ptr_t *head)
{
   struct lr_retired *node = LR_CAST(struct lr_retired*, retro_atomic_exchange_ptr(head, NULL));
   while (node)
   {
      struct lr_retired *next = node->next;
      node->free_cb(node->owner);
      node = next;
   }
}

#endif
