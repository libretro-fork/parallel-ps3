/* See eeprom_table.h */

#include "eeprom_table.h"

/* A word: the offset plus one above the byte, so an empty word is zero */
#define EEPROM_WORD(offset, value) \
   ((int64_t)((((uint64_t)(offset) + 1) << 8) | (uint64_t)(value)))
#define EEPROM_WORD_KEY(word) ((uint64_t)(word) >> 8)

static unsigned eeprom_start(uint32_t offset)
{
   return (unsigned)((offset * 2654435769u) >> 22) % EEPROM_TABLE_SLOTS;
}

uint8_t eeprom_table_read(const struct eeprom_table *table, uint32_t offset)
{
   uint64_t key = (uint64_t)offset + 1;
   unsigned at  = eeprom_start(offset);
   unsigned n;

   for (n = 0; n < EEPROM_TABLE_SLOTS; n++)
   {
      int64_t word = retro_atomic_load_acquire_64(
            (retro_atomic_64_t*)&table->slots[(at + n) % EEPROM_TABLE_SLOTS]);

      if (!word)
         break;
      if (EEPROM_WORD_KEY(word) == key)
         return (uint8_t)word;
   }

   return 0xFF;
}

int eeprom_table_write(struct eeprom_table *table, uint32_t offset,
      uint8_t value)
{
   uint64_t key = (uint64_t)offset + 1;
   unsigned at  = eeprom_start(offset);
   unsigned n;

   for (n = 0; n < EEPROM_TABLE_SLOTS; n++)
   {
      retro_atomic_64_t *slot = &table->slots[(at + n) % EEPROM_TABLE_SLOTS];

      for (;;)
      {
         int64_t word = retro_atomic_load_acquire_64(slot);

         /* Another offset's: on to the next word */
         if (word && EEPROM_WORD_KEY(word) != key)
            break;

         /* Empty, or this offset's: one swap claims it or replaces the
          * byte; a swap that fails looks at the word again */
         if (retro_atomic_cas_64(slot, word, EEPROM_WORD(offset, value)))
            return 1;
      }
   }

   return 0;
}
