/* The object cache's pack file (Utilities/object_pack.c), one file of
 * compiled objects a directory:
 *
 * - records written one after another are all found again, each with its
 *   name, its place and its length, and each object lies at a multiple of
 *   16 bytes;
 * - a record cut short anywhere - its end mark missing, its lengths running
 *   past the end of the file - ends the file there: everything before it
 *   counts, nothing after it, and the length returned is where the next
 *   record is to be written;
 * - a file that does not start as a pack holds nothing;
 * - in the index, a name added twice finds the one added last;
 * - threads adding to the index while others look names up: every name
 *   added is found, with its own entry. (Built a second time with
 *   ThreadSanitizer.)
 *
 * Usage: test_object_pack */

#include "../../../Utilities/object_pack.h"
#include "test_common.h"

#include <pthread.h>

#define OBJECTS 300

static uint8_t file[4 << 20];

static size_t object_len(int i) { return (size_t)(i * 37 % 5000) + 1; }

static int object_name(char *name, int i)
{
   return sprintf(name, "v8-kusa-%d-of-the-module.obj", i * 7919);
}

/* Appends a record as the cache does: the start, the object, the end */
static uint64_t put(uint64_t at, int i)
{
   char name[64];
   uint32_t name_len = (uint32_t)object_name(name, i);
   size_t len        = object_len(i);
   uint64_t off      = object_pack_record_begin(file + at, name, name_len, len);
   size_t k;
   for (k = 0; k < len; k++)
      file[at + off + k] = (uint8_t)(i + k);
   object_pack_record_end(file + at + off + len, name_len, len);
   return at + object_pack_record_size(name_len, len);
}

static uint64_t fill(void)
{
   uint64_t at = OBJECT_PACK_FILE_HEADER;
   int i;
   memset(file, 0xEE, sizeof(file));
   object_pack_file_header(file);
   for (i = 0; i < OBJECTS; i++)
      at = put(at, i);
   return at;
}

static void records(void)
{
   static struct object_pack_entry entries[OBJECTS];
   size_t count   = 0;
   uint64_t end   = fill();
   uint64_t valid = object_pack_scan(file, end, NULL, 0, &count);
   int i;

   TEST_CHECK(valid == end);
   TEST_CHECK(count == OBJECTS);

   valid = object_pack_scan(file, end, entries, OBJECTS, &count);
   TEST_CHECK(valid == end && count == OBJECTS);
   for (i = 0; i < OBJECTS; i++)
   {
      char name[64];
      uint32_t name_len = (uint32_t)object_name(name, i);
      size_t k, bad = 0;
      TEST_CHECK(entries[i].name_len == name_len);
      TEST_CHECK(memcmp(entries[i].name, name, name_len) == 0);
      TEST_CHECK(entries[i].size == object_len(i));
      TEST_CHECK(entries[i].offset % 16 == 0);
      for (k = 0; k < entries[i].size; k++)
         bad += file[entries[i].offset + k] != (uint8_t)(i + k);
      TEST_CHECK(bad == 0);
   }

   /* Past what was written: the fill, not a record */
   valid = object_pack_scan(file, sizeof(file), NULL, 0, &count);
   TEST_CHECK(valid == end && count == OBJECTS);
}

static void cut_short(void)
{
   uint64_t end = fill();
   uint64_t starts[OBJECTS + 1];
   uint64_t at  = OBJECT_PACK_FILE_HEADER;
   size_t count = 0;
   uint64_t valid, len;
   int i;

   for (i = 0; i < OBJECTS; i++)
   {
      char name[64];
      starts[i] = at;
      at += object_pack_record_size((uint32_t)object_name(name, i), object_len(i));
   }
   starts[OBJECTS] = at;
   TEST_CHECK(at == end);

   /* The file ending anywhere inside record 200 */
   for (len = starts[200] + 1; len < starts[201]; len += 7)
   {
      valid = object_pack_scan(file, len, NULL, 0, &count);
      if (valid != starts[200] || count != 200)
      {
         TEST_CHECK(valid == starts[200] && count == 200);
         break;
      }
   }

   /* Record 100 never written (its place reserved, then the write failed):
    * the records after it do not count, and the next goes where it was */
   memset(file + starts[100], 0, (size_t)(starts[101] - starts[100]));
   valid = object_pack_scan(file, end, NULL, 0, &count);
   TEST_CHECK(valid == starts[100] && count == 100);

   /* Its start written but not its end mark */
   fill();
   memset(file + starts[101] - 16, 0, 16);
   valid = object_pack_scan(file, end, NULL, 0, &count);
   TEST_CHECK(valid == starts[100] && count == 100);

   /* Its end mark's lengths right but not the mark itself */
   fill();
   file[starts[101] - 16] ^= 0x40;
   valid = object_pack_scan(file, end, NULL, 0, &count);
   TEST_CHECK(valid == starts[100] && count == 100);

   /* A length that runs past the end of the file, or wraps round */
   fill();
   {
      uint64_t huge = ~(uint64_t)0 - 20;
      memcpy(file + starts[50] + 8, &huge, sizeof(huge));
      valid = object_pack_scan(file, end, NULL, 0, &count);
      TEST_CHECK(valid == starts[50] && count == 50);
      huge = end;
      memcpy(file + starts[50] + 8, &huge, sizeof(huge));
      valid = object_pack_scan(file, end, NULL, 0, &count);
      TEST_CHECK(valid == starts[50] && count == 50);
   }

   /* Not a pack */
   fill();
   file[3] ^= 1;
   valid = object_pack_scan(file, end, NULL, 0, &count);
   TEST_CHECK(valid == 0 && count == 0);
   TEST_CHECK(!object_pack_file_valid(file, end));
   fill();
   TEST_CHECK(object_pack_file_valid(file, end));
   TEST_CHECK(!object_pack_file_valid(file, 15));
   valid = object_pack_scan(file, 0, NULL, 0, &count);
   TEST_CHECK(valid == 0 && count == 0);
}

/* The index, from several threads */
#define ADDERS   4
#define PER_ADDER 5000

static struct object_pack_index index_;
static struct object_pack_entry added[ADDERS][PER_ADDER];
static char names[ADDERS][PER_ADDER][24];
static retro_atomic_int_t done[ADDERS];
static retro_atomic_int_t wrong;

static void *adder(void *arg)
{
   int t = (int)(size_t)arg;
   int i;
   for (i = 0; i < PER_ADDER; i++)
   {
      struct object_pack_entry *e = &added[t][i];
      e->name     = names[t][i];
      e->name_len = (uint32_t)sprintf(names[t][i], "obj-%d-%d", t, i);
      e->offset   = (uint64_t)t * PER_ADDER + i;
      e->size     = 1;
      object_pack_index_add(&index_, e);
      retro_atomic_store_release_int(&done[t], i + 1);
   }
   return NULL;
}

static void *finder(void *arg)
{
   int rounds;
   (void)arg;
   for (rounds = 0; rounds < 40; rounds++)
   {
      int t;
      for (t = 0; t < ADDERS; t++)
      {
         /* Everything this adder had added when asked is there */
         int n = retro_atomic_load_acquire_int(&done[t]);
         int i;
         for (i = 0; i < n; i += 13)
         {
            char name[24];
            uint32_t len = (uint32_t)sprintf(name, "obj-%d-%d", t, i);
            const struct object_pack_entry *e =
               object_pack_index_find(&index_, name, len);
            if (!e || e != &added[t][i])
               retro_atomic_fetch_add_int(&wrong, 1);
         }
      }
   }
   return NULL;
}

static void index_threads(void)
{
   pthread_t add[ADDERS], find[2];
   struct object_pack_entry again;
   int t, i, missing = 0;

   object_pack_index_init(&index_);
   TEST_CHECK(object_pack_index_find(&index_, "obj-0-0", 7) == NULL);

   for (t = 0; t < ADDERS; t++)
      pthread_create(&add[t], NULL, adder, (void*)(size_t)t);
   for (t = 0; t < 2; t++)
      pthread_create(&find[t], NULL, finder, NULL);
   for (t = 0; t < ADDERS; t++)
      pthread_join(add[t], NULL);
   for (t = 0; t < 2; t++)
      pthread_join(find[t], NULL);

   TEST_CHECK(retro_atomic_load_acquire_int(&wrong) == 0);
   for (t = 0; t < ADDERS; t++)
      for (i = 0; i < PER_ADDER; i++)
         missing += object_pack_index_find(&index_, names[t][i],
               added[t][i].name_len) != &added[t][i];
   TEST_CHECK(missing == 0);
   TEST_CHECK(object_pack_index_find(&index_, "obj-0-0x", 8) == NULL);
   TEST_CHECK(object_pack_index_find(&index_, "obj-0-", 6) == NULL);

   /* The same name again: the later one */
   again.name     = "obj-2-77";
   again.name_len = 8;
   again.offset   = 123456;
   again.size     = 9;
   object_pack_index_add(&index_, &again);
   TEST_CHECK(object_pack_index_find(&index_, "obj-2-77", 8) == &again);
}

int main(void)
{
   records();
   cut_short();
   index_threads();
   printf("test_object_pack: %s\n", test_failures ? "FAIL" : "ok");
   return test_failures ? 1 : 0;
}
