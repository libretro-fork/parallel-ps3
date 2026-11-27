#ifndef RPCS3_UTIL_VIEW_CACHE_H
#define RPCS3_UTIL_VIEW_CACHE_H

/* The image views made in a frame for textures put together from several
 * surfaces, found again by the texture's address and its properties: one
 * open-addressed table of plain entries, emptied at the end of the frame
 * with its memory kept. It was a std::unordered_multimap of the whole
 * request (its list of copies included, copied for every view kept) with
 * a node allocated for each and all of them freed every frame
 * (Emu/RSX/Common/texture_cache.h). The renderer's thread alone. C89.
 *
 * An entry is empty while its view is NULL, and dead (taken out during
 * the frame, still part of its probe run) while its view is
 * VIEW_CACHE_DEAD. Plain data: zeroed, a cache is empty; freed, it is
 * empty again. */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
#define VIEW_CACHE_DEAD (reinterpret_cast<void*>(static_cast<size_t>(1)))
#else
#define VIEW_CACHE_DEAD ((void*)(size_t)1)
#endif

typedef struct view_cache_entry
{
   uint64_t props;        /* the request's encoded properties */
   uint32_t address;
   uint32_t remap;        /* the channel remap the view was made with */
   uint32_t range_start;  /* the guest memory the view was made from */
   uint32_t range_end;
   void    *view;
} view_cache_entry_t;

typedef struct view_cache
{
   view_cache_entry_t *e;
   uint32_t cap;    /* a power of two, or 0 before the first add */
   uint32_t used;   /* entries not empty: live and dead */
   uint32_t live;
} view_cache_t;

/* Whether @e holds a view */
#define VIEW_CACHE_LIVE(e) ((e)->view > VIEW_CACHE_DEAD)

/* The live entry for @address and @props, or NULL */
view_cache_entry_t *view_cache_find(const view_cache_t *c, uint32_t address, uint64_t props);

/* A view kept (the caller has found none for the address and properties).
 * 1: added; 0: out of memory, or @view NULL or VIEW_CACHE_DEAD, nothing
 * changed. Entries may move: pointers from view_cache_find end here. */
int view_cache_add(view_cache_t *c, uint32_t address, uint64_t props, uint32_t remap,
      uint32_t range_start, uint32_t range_end, void *view);

/* A live entry taken out (its view is the caller's to release first) */
void view_cache_kill(view_cache_t *c, view_cache_entry_t *e);

/* Emptied, its memory kept for the next frame */
void view_cache_clear(view_cache_t *c);

/* Its memory freed, the cache empty */
void view_cache_free(view_cache_t *c);

#ifdef __cplusplus
}
#endif

#endif
