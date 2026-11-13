/* See object_pack.h */

#include "object_pack.h"

#include <stdlib.h>
#include <string.h>

#define PACK_FILE_MAGIC   "PS3OBJPK"
#define PACK_FILE_VERSION 1u
#define PACK_RECORD_MAGIC 0x4B50424Fu /* "OBPK" */
#define PACK_RECORD_END   0x444E4550u /* "PEND" */

#define PACK_ALIGN(n) (((n) + 15u) & ~(uint64_t)15u)

/* A record's first and last 16 bytes */
struct pack_mark
{
   uint32_t magic;
   uint32_t name_len;
   uint64_t data_len;
};

void object_pack_file_header(uint8_t *dst)
{
   uint32_t version = PACK_FILE_VERSION;
   memset(dst, 0, OBJECT_PACK_FILE_HEADER);
   memcpy(dst, PACK_FILE_MAGIC, 8);
   memcpy(dst + 8, &version, sizeof(version));
}

int object_pack_file_valid(const uint8_t *image, uint64_t len)
{
   uint8_t header[OBJECT_PACK_FILE_HEADER];
   if (len < OBJECT_PACK_FILE_HEADER)
      return 0;
   object_pack_file_header(header);
   return memcmp(image, header, sizeof(header)) == 0;
}

uint64_t object_pack_record_size(uint32_t name_len, uint64_t data_len)
{
   return sizeof(struct pack_mark) + PACK_ALIGN((uint64_t)name_len)
      + PACK_ALIGN(data_len) + sizeof(struct pack_mark);
}

uint64_t object_pack_record_begin(uint8_t *dst, const char *name,
      uint32_t name_len, uint64_t data_len)
{
   struct pack_mark mark;
   uint64_t name_room = PACK_ALIGN((uint64_t)name_len);

   mark.magic    = PACK_RECORD_MAGIC;
   mark.name_len = name_len;
   mark.data_len = data_len;
   memcpy(dst, &mark, sizeof(mark));
   memcpy(dst + sizeof(mark), name, name_len);
   memset(dst + sizeof(mark) + name_len, 0, (size_t)(name_room - name_len));
   return sizeof(mark) + name_room;
}

void object_pack_record_end(uint8_t *dst, uint32_t name_len,
      uint64_t data_len)
{
   struct pack_mark mark;
   size_t pad = (size_t)(PACK_ALIGN(data_len) - data_len);

   mark.magic    = PACK_RECORD_END;
   mark.name_len = name_len;
   mark.data_len = data_len;
   memset(dst, 0, pad);
   memcpy(dst + pad, &mark, sizeof(mark));
}

static uint64_t pack_hash(const char *name, uint32_t name_len)
{
   /* FNV-1a */
   uint64_t h = 0xCBF29CE484222325ull;
   uint32_t i;
   for (i = 0; i < name_len; i++)
   {
      h ^= (uint8_t)name[i];
      h *= 0x100000001B3ull;
   }
   return h;
}

uint64_t object_pack_scan(const uint8_t *image, uint64_t len,
      struct object_pack_entry *entries, size_t cap, size_t *count)
{
   uint64_t at = OBJECT_PACK_FILE_HEADER;
   size_t n    = 0;

   *count = 0;
   if (!object_pack_file_valid(image, len))
      return 0;

   for (;;)
   {
      struct pack_mark mark, end;
      uint64_t name_room, data_room, left;

      left = len - at;
      if (left < 2 * sizeof(mark))
         break;
      memcpy(&mark, image + at, sizeof(mark));
      if (mark.magic != PACK_RECORD_MAGIC || !mark.name_len)
         break;

      /* Lengths that fit in what is left, without wrapping round */
      name_room = PACK_ALIGN((uint64_t)mark.name_len);
      left     -= 2 * sizeof(mark);
      if (name_room > left || mark.data_len > left - name_room)
         break;
      data_room = PACK_ALIGN(mark.data_len);
      if (data_room > left - name_room)
         break;

      memcpy(&end, image + at + sizeof(mark) + name_room + data_room,
            sizeof(end));
      if (end.magic != PACK_RECORD_END || end.name_len != mark.name_len
            || end.data_len != mark.data_len)
         break;

      if (n < cap)
      {
         struct object_pack_entry *e = &entries[n];
         e->next     = NULL;
         e->name     = (const char*)(image + at + sizeof(mark));
         e->name_len = mark.name_len;
         e->hash     = 0;
         e->offset   = at + sizeof(mark) + name_room;
         e->size     = mark.data_len;
      }
      n++;
      at += 2 * sizeof(mark) + name_room + data_room;
   }

   *count = n;
   return at;
}

void object_pack_index_init(struct object_pack_index *index)
{
   size_t i;
   for (i = 0; i < OBJECT_PACK_BUCKETS; i++)
      retro_atomic_ptr_init(&index->buckets[i], NULL);
}

void object_pack_index_add(struct object_pack_index *index,
      struct object_pack_entry *entry)
{
   retro_atomic_ptr_t *bucket;

   entry->hash = pack_hash(entry->name, entry->name_len);
   bucket      = &index->buckets[entry->hash & (OBJECT_PACK_BUCKETS - 1)];

   /* Entries are only ever added, at the head, so a reader that has the
    * head has a list that does not change under it */
   for (;;)
   {
      void *head  = retro_atomic_load_acquire_ptr(bucket);
      entry->next = (struct object_pack_entry*)head;
      if (retro_atomic_cas_ptr(bucket, head, entry))
         return;
   }
}

const struct object_pack_entry *object_pack_index_find(
      const struct object_pack_index *index, const char *name,
      uint32_t name_len)
{
   uint64_t hash = pack_hash(name, name_len);
   const struct object_pack_entry *e = (const struct object_pack_entry*)
      retro_atomic_load_acquire_ptr((retro_atomic_ptr_t*)
            &index->buckets[hash & (OBJECT_PACK_BUCKETS - 1)]);

   for (; e; e = e->next)
   {
      if (e->hash == hash && e->name_len == name_len
            && memcmp(e->name, name, name_len) == 0)
         return e;
   }
   return NULL;
}

/* The image's block starts with its count of holds; the image itself is 64
 * bytes in, so objects lie at multiples of 16 as they do in the file */
#define PACK_IMAGE_HEAD 64

static uint8_t *pack_image_new(uint64_t len)
{
   uint8_t *block = (uint8_t*)malloc(PACK_IMAGE_HEAD + (size_t)len);
   if (!block)
      return NULL;
   retro_atomic_int_init((retro_atomic_int_t*)block, 1);
   return block + PACK_IMAGE_HEAD;
}

void object_pack_image_take(uint8_t *image)
{
   retro_atomic_fetch_add_int(
         (retro_atomic_int_t*)(image - PACK_IMAGE_HEAD), 1);
}

void object_pack_image_drop(uint8_t *image)
{
   if (retro_atomic_fetch_sub_int(
            (retro_atomic_int_t*)(image - PACK_IMAGE_HEAD), 1) == 1)
      free(image - PACK_IMAGE_HEAD);
}

void object_pack_open(struct object_pack *pack,
      const struct object_pack_io *io)
{
   uint64_t valid = 0;
   uint64_t size;

   memset(&pack->io, 0, sizeof(pack->io));
   if (io)
      pack->io = *io;
   pack->image         = NULL;
   pack->lent_len      = 0;
   pack->scanned       = NULL;
   pack->scanned_count = 0;
   object_pack_index_init(&pack->index);
   retro_atomic_store_release_64(&pack->end, OBJECT_PACK_FILE_HEADER);

   if (!pack->io.file)
      return;

   size = pack->io.size(pack->io.file);
   if (size)
   {
      /* The whole pack in one read */
      uint8_t *image = pack_image_new(size);
      if (image && pack->io.read_at(pack->io.file, 0, image, size) == size)
      {
         size_t count = 0;
         valid = object_pack_scan(image, size, NULL, 0, &count);
         if (count)
            pack->scanned = (struct object_pack_entry*)
               malloc(count * sizeof(*pack->scanned));
         if (pack->scanned)
         {
            size_t i;
            object_pack_scan(image, size, pack->scanned, count, &count);
            pack->scanned_count = count;
            pack->image         = image;
            pack->lent_len      = valid;
            image               = NULL;
            for (i = 0; i < count; i++)
               object_pack_index_add(&pack->index, &pack->scanned[i]);
         }
      }
      if (image)
         object_pack_image_drop(image);
   }

   if (!valid)
   {
      /* New, or not a pack: started over */
      uint8_t header[OBJECT_PACK_FILE_HEADER];
      object_pack_file_header(header);
      pack->io.write_at(pack->io.file, 0, header, sizeof(header));
      valid = OBJECT_PACK_FILE_HEADER;
   }

   retro_atomic_store_release_64(&pack->end, (int64_t)valid);
}

void object_pack_close(struct object_pack *pack)
{
   size_t i;

   /* The entries added since the scan are allocations of their own */
   for (i = 0; i < OBJECT_PACK_BUCKETS; i++)
   {
      struct object_pack_entry *e = (struct object_pack_entry*)
         retro_atomic_load_acquire_ptr(&pack->index.buckets[i]);
      while (e)
      {
         struct object_pack_entry *next = e->next;
         if (e < pack->scanned || e >= pack->scanned + pack->scanned_count)
            free(e);
         e = next;
      }
   }
   object_pack_index_init(&pack->index);

   free(pack->scanned);
   pack->scanned       = NULL;
   pack->scanned_count = 0;
   if (pack->image)
      object_pack_image_drop(pack->image);
   pack->image    = NULL;
   pack->lent_len = 0;
   pack->io.file  = NULL;
}

const struct object_pack_entry *object_pack_find(
      const struct object_pack *pack, const char *name, uint32_t name_len)
{
   return object_pack_index_find(&pack->index, name, name_len);
}

const struct object_pack_entry *object_pack_add(struct object_pack *pack,
      const char *name, uint32_t name_len, const void *data, uint64_t size)
{
   uint8_t head[16 + 240];
   uint8_t tail[16 + 16];
   struct object_pack_entry *entry;
   uint64_t total, at, head_len, tail_len;
   char *entry_name;

   if (!pack->io.file || !name_len || name_len > 240)
      return NULL;

   entry = (struct object_pack_entry*)malloc(sizeof(*entry) + name_len);
   if (!entry)
      return NULL;

   /* Its place, taken with one add: two threads' records neither overlap
    * nor wait for one another */
   total    = object_pack_record_size(name_len, size);
   at       = (uint64_t)retro_atomic_fetch_add_64(&pack->end, (int64_t)total);
   head_len = object_pack_record_begin(head, name, name_len, size);
   tail_len = total - head_len - size;
   object_pack_record_end(tail, name_len, size);

   if (pack->io.write_at(pack->io.file, at, head, head_len) != head_len
         || pack->io.write_at(pack->io.file, at + head_len, data, size) != size
         || pack->io.write_at(pack->io.file, at + head_len + size, tail,
            tail_len) != tail_len)
   {
      /* The pack counts up to here the next time it is read */
      free(entry);
      return NULL;
   }

   entry_name = (char*)(entry + 1);
   memcpy(entry_name, name, name_len);
   entry->name     = entry_name;
   entry->name_len = name_len;
   entry->offset   = at + head_len;
   entry->size     = size;
   object_pack_index_add(&pack->index, entry);
   return entry;
}

const uint8_t *object_pack_lent(const struct object_pack *pack,
      const struct object_pack_entry *entry)
{
   if (!pack->image || entry->offset + entry->size > pack->lent_len)
      return NULL;
   return pack->image + entry->offset;
}

int object_pack_read(const struct object_pack *pack,
      const struct object_pack_entry *entry, void *dst)
{
   if (!pack->io.file)
      return 0;
   return pack->io.read_at(pack->io.file, entry->offset, dst, entry->size)
      == entry->size;
}

void object_pack_each(const struct object_pack *pack, object_pack_each_t fn,
      void *ud)
{
   size_t i;
   for (i = 0; i < OBJECT_PACK_BUCKETS; i++)
   {
      const struct object_pack_entry *e = (const struct object_pack_entry*)
         retro_atomic_load_acquire_ptr(
               (retro_atomic_ptr_t*)&pack->index.buckets[i]);
      for (; e; e = e->next)
      {
         /* Not one a later record of the same name stands in front of */
         if (object_pack_index_find(&pack->index, e->name, e->name_len) == e)
            fn(ud, e);
      }
   }
}
