/* The bytes a game has written to the console's EEPROM (sys_ss's update
 * manager): offset to value, read and written by any number of threads with
 * no lock.
 *
 * A fixed table of words, each an offset and its byte, found by hashing the
 * offset and walking on from there. A word is claimed for an offset once, with
 * a compare-and-swap, and keeps it; its byte is replaced the same way. An
 * offset that was never written reads as 0xFF, which is also what unsetting
 * one writes. Zeroed memory is an empty table. */

#ifndef EEPROM_TABLE_H
#define EEPROM_TABLE_H

#include <stdint.h>

#include <retro_atomic.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EEPROM_TABLE_SLOTS 1024

struct eeprom_table
{
   retro_atomic_64_t slots[EEPROM_TABLE_SLOTS];
};

/* The byte at offset; 0xFF when none was written */
uint8_t eeprom_table_read(const struct eeprom_table *table, uint32_t offset);

/* Sets the byte at offset. 0 when the table has no word left for a new
 * offset (the write is dropped), 1 otherwise. */
int eeprom_table_write(struct eeprom_table *table, uint32_t offset,
      uint8_t value);

#ifdef __cplusplus
}
#endif

#endif
