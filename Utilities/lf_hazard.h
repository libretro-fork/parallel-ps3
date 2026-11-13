/* Hazard cells: a thread says which object it is looking at, so that the
 * thread that takes the object away knows to wait before freeing it.
 *
 * For a table of objects that is read far more often than it changes and
 * whose readers should neither take a lock nor count a reference. A
 * reader loads the pointer from the table, puts it in a cell of its own,
 * loads the pointer again and goes on only if it is still the same: from
 * then until it empties the cell the object will not be freed. Whoever
 * takes the pointer out of the table then waits until lf_hazard_any()
 * says no cell holds it.
 *
 * Each thread claims one record of a few cells (a few, so that it can
 * look at one object while looking at another) and writes only its own:
 * holding an object is one exchange on a cache line no other thread
 * writes. */

#ifndef LF_HAZARD_H
#define LF_HAZARD_H

#ifdef __cplusplus
extern "C" {
#endif

#define LF_HAZARD_THREADS 256
#define LF_HAZARD_CELLS   4

/* A record for the calling thread: its number, or -1 when all are
 * taken (the thread then has to keep objects alive some other way) */
int lf_hazard_claim(void);

/* Gives the record back; its cells must be empty */
void lf_hazard_release(int record);

/* Puts p (not NULL) in an empty cell of the record and returns the cell,
 * or -1 when the record has none empty. Only the record's own thread.
 * The pointer is announced before this returns: load it from the table
 * again afterwards, and drop the cell if it changed. */
int lf_hazard_hold(int record, void *p);

/* Empties the cell */
void lf_hazard_drop(int record, int cell);

/* Nonzero while some cell holds p. For the thread that took p out of the
 * table, after it did. */
int lf_hazard_any(const void *p);

#ifdef __cplusplus
}
#endif

#endif
