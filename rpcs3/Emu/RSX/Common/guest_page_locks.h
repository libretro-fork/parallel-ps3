/* Which pages of guest memory the texture cache has locked, and the notes
 * guest threads leave for it - all with no lock.
 *
 * The texture cache protects the memory its sections were made from: pages
 * of plain textures read-only, pages of sections that have to be written
 * back before the guest may see them (render targets) no-access. Here it
 * keeps, per 4 KiB page, how many sections have the page locked each way.
 *
 * A guest thread that faults writing a page which only textures have
 * locked needs nothing from the cache but to have it told: it makes the
 * page writable itself, leaves a note with the page and goes on. The
 * renderer takes the notes the next time it enters the cache and does the
 * bookkeeping then.
 *
 * The cache's lock used to keep a fault and a lock of the same page from
 * interleaving. Here a lock is counted before its protection is applied,
 * and applying a protection waits for a guest thread that has the page in
 * hand: a lock that comes while a guest thread is making the page
 * writable ends up applied after that, and holds. */

#ifndef RSX_GUEST_PAGE_LOCKS_H
#define RSX_GUEST_PAGE_LOCKS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Notes that can wait at a time; when there is no room the fault is
 * handled the long way */
#define RSX_PAGE_NOTES 4096

/* No page locked by the cache, no note; what rsx_page_locks_hold holds
 * stays held. For when the cache is emptied, with nothing else running
 * in it. */
void rsx_page_locks_reset(void);

/* One section more, or one fewer, has the pages from the one holding
 * address first to the one holding address last locked - no-access, or
 * else read-only */
void rsx_page_locks_add(uint32_t first, uint32_t last, int no_access);
void rsx_page_locks_remove(uint32_t first, uint32_t last, int no_access);

/* The pages are locked by somebody other than the texture cache - one
 * owner a page - who has to deal with a fault there itself: a guest write
 * does not unlock such a page on its own, whatever textures share it.
 * Released the same way; releasing a page that is not held, or that
 * rsx_page_locks_reset has let go, does nothing. To be settled before the
 * protection is applied, like a lock that is counted. */
void rsx_page_locks_hold(uint32_t first, uint32_t last);
void rsx_page_locks_release(uint32_t first, uint32_t last);

/* For a guest thread that faulted writing to address.
 *
 * 1: the page is locked read-only, by nothing no-access and not held, and is the
 * caller's until rsx_page_locks_end_write: the caller makes the page
 * writable and then calls that, which leaves the note. While a page is a
 * guest thread's this way, nobody else changes its protection
 * (rsx_page_locks_settle) - so a page that something comes to lock
 * meanwhile is locked after the guest thread made it writable, not before,
 * and stays locked.
 *
 * 2: another guest thread had the page this way and is done with it: it
 * is writable, and that thread has left the note or has gone the long way.
 *
 * 0: the page is not of that kind: nothing was done. */
int rsx_page_locks_begin_write(uint32_t address);

/* The page is writable now: leaves the note and gives the page up. 0 when
 * there was no room for a note - the caller has the fault handled the
 * long way, which finds the page writable and does the bookkeeping. */
int rsx_page_locks_end_write(uint32_t address);

/* For whoever is about to protect the pages from the one holding first to
 * the one holding last (read-only or no-access), after counting the lock
 * with rsx_page_locks_add: waits for guest threads that have one of the
 * pages between rsx_page_locks_begin_write and rsx_page_locks_end_write -
 * the time of one protection call each. */
void rsx_page_locks_settle(uint32_t first, uint32_t last);

/* Nonzero when notes are waiting: one load */
int rsx_page_locks_pending(void);

/* Takes a note: the address of its page. 0 when there is none. */
int rsx_page_locks_take(uint32_t *page_address);

/* How many sections have the page of address locked read-only, and
 * no-access */
void rsx_page_locks_get(uint32_t address, uint32_t *read_only,
      uint32_t *no_access);

#ifdef __cplusplus
}
#endif

#endif
