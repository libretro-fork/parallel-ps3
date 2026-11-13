/* See object_pack.h */

#include "object_pack.h"

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
