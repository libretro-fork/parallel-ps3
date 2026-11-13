/* The EEPROM table of sys_ss (Emu/Cell/lv2/eeprom_table.c): offset to byte
 * with no lock.
 *
 * - an offset never written reads 0xFF; one written reads what was written
 *   last; writing 0xFF unsets it; offsets that hash together do not get in
 *   one another's way;
 * - the table takes as many offsets as it has words and refuses the next,
 *   which leaves the others as they were;
 * - four threads writing their own offsets while they and two more read:
 *   every read gives a byte its offset's writer wrote (or 0xFF before the
 *   first), and at the end each offset holds its last. (Built a second time
 *   with ThreadSanitizer.)
 *
 * Usage: test_eeprom_table */

#include "../../Emu/Cell/lv2/eeprom_table.h"
#include "test_common.h"

#include <pthread.h>

static struct eeprom_table table;

static void alone(void)
{
   uint32_t i;
   int refused = 0, wrong = 0;

   TEST_CHECK(eeprom_table_read(&table, 0x48C18) == 0xFF);
   TEST_CHECK(eeprom_table_write(&table, 0x48C18, 0x00));
   TEST_CHECK(eeprom_table_write(&table, 0x48C1B, 0x01));
   TEST_CHECK(eeprom_table_read(&table, 0x48C18) == 0x00);
   TEST_CHECK(eeprom_table_read(&table, 0x48C1B) == 0x01);
   TEST_CHECK(eeprom_table_read(&table, 0x48C1A) == 0xFF);
   TEST_CHECK(eeprom_table_write(&table, 0x48C1B, 0x7E));
   TEST_CHECK(eeprom_table_read(&table, 0x48C1B) == 0x7E);
   TEST_CHECK(eeprom_table_write(&table, 0x48C1B, 0xFF));
   TEST_CHECK(eeprom_table_read(&table, 0x48C1B) == 0xFF);
   TEST_CHECK(eeprom_table_write(&table, 0, 0x33));
   TEST_CHECK(eeprom_table_write(&table, 0xFFFFFFFFu, 0x44));
   TEST_CHECK(eeprom_table_read(&table, 0) == 0x33);
   TEST_CHECK(eeprom_table_read(&table, 0xFFFFFFFFu) == 0x44);

   /* Until it is full: four offsets are in already */
   for (i = 0; i < EEPROM_TABLE_SLOTS; i++)
      refused += !eeprom_table_write(&table, 0x100000 + i * 4096, (uint8_t)i);
   TEST_CHECK(refused == 4);
   for (i = 0; i < EEPROM_TABLE_SLOTS - 4; i++)
      wrong += eeprom_table_read(&table, 0x100000 + i * 4096) != (uint8_t)i;
   TEST_CHECK(wrong == 0);
   TEST_CHECK(eeprom_table_read(&table, 0x100000 + (EEPROM_TABLE_SLOTS - 1) * 4096) == 0xFF);
   TEST_CHECK(eeprom_table_read(&table, 0) == 0x33);

   /* What is there can still be changed */
   TEST_CHECK(eeprom_table_write(&table, 0x100000, 0x55));
   TEST_CHECK(eeprom_table_read(&table, 0x100000) == 0x55);
}

#define WRITERS 4
#define OFFSETS 100
#define ROUNDS  2000

static retro_atomic_int_t bad;

/* Writer t writes bytes whose top two bits are t */
static void *writer(void *arg)
{
   uint32_t t = (uint32_t)(size_t)arg;
   uint32_t r, i;
   for (r = 0; r < ROUNDS; r++)
   {
      for (i = 0; i < OFFSETS; i++)
      {
         uint32_t offset = 0x48000 + i * WRITERS + t;
         uint8_t  seen;
         eeprom_table_write(&table, offset, (uint8_t)(t << 6 | (r & 0x3F)));
         seen = eeprom_table_read(&table, offset);
         if (seen != (uint8_t)(t << 6 | (r & 0x3F)))
            retro_atomic_fetch_add_int(&bad, 1);
      }
   }
   return NULL;
}

static void *reader(void *arg)
{
   uint32_t r, i;
   (void)arg;
   for (r = 0; r < ROUNDS; r++)
   {
      for (i = 0; i < OFFSETS * WRITERS; i++)
      {
         uint8_t seen = eeprom_table_read(&table, 0x48000 + i);
         if (seen != 0xFF && (uint32_t)(seen >> 6) != i % WRITERS)
            retro_atomic_fetch_add_int(&bad, 1);
      }
   }
   return NULL;
}

static void together(void)
{
   pthread_t w[WRITERS], rd[2];
   uint32_t t, i;
   int wrong = 0;

   memset(&table, 0, sizeof(table));
   for (t = 0; t < WRITERS; t++)
      pthread_create(&w[t], NULL, writer, (void*)(size_t)t);
   for (t = 0; t < 2; t++)
      pthread_create(&rd[t], NULL, reader, NULL);
   for (t = 0; t < WRITERS; t++)
      pthread_join(w[t], NULL);
   for (t = 0; t < 2; t++)
      pthread_join(rd[t], NULL);

   TEST_CHECK(retro_atomic_load_acquire_int(&bad) == 0);
   for (i = 0; i < OFFSETS * WRITERS; i++)
      wrong += eeprom_table_read(&table, 0x48000 + i)
         != (uint8_t)((i % WRITERS) << 6 | ((ROUNDS - 1) & 0x3F));
   TEST_CHECK(wrong == 0);
}

int main(void)
{
   alone();
   together();
   printf("test_eeprom_table: %s\n", test_failures ? "FAIL" : "ok");
   return test_failures ? 1 : 0;
}
