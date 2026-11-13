/* A directory's compiled objects in one file (the PPU and SPU recompilers'
 * object caches): the layout of that file and the index over it.
 *
 * The file starts with a 16-byte header and is a run of records after it,
 * each written whole in one write at an offset its writer reserved:
 *
 *    16 bytes   magic, length of the name, length of the object
 *    name       padded with zeros to 16 bytes
 *    object     padded with zeros to 16 bytes
 *    16 bytes   end mark, the two lengths again
 *
 * A record cut short has no end mark, and the file counts up to the first
 * record that is not whole; what follows is written over. Lengths are in the
 * machine's byte order: the objects are that machine's code.
 *
 * The index is added to and read by any number of threads with no lock. */

#ifndef OBJECT_PACK_H
#define OBJECT_PACK_H

#include <stddef.h>
#include <stdint.h>

#include <retro_atomic.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OBJECT_PACK_FILE_HEADER 16
#define OBJECT_PACK_BUCKETS     4096

struct object_pack_entry
{
   struct object_pack_entry *next; /* set before the entry is added */
   const char *name;               /* not terminated */
   uint64_t hash;
   uint64_t offset;                /* of the object in the file */
   uint64_t size;
   uint32_t name_len;
};

struct object_pack_index
{
   retro_atomic_ptr_t buckets[OBJECT_PACK_BUCKETS];
};

/* The 16 bytes a pack file starts with */
void object_pack_file_header(uint8_t *dst);

/* 1 when image, of len bytes, starts as a pack file does */
int object_pack_file_valid(const uint8_t *image, uint64_t len);

/* The size of the record for a name and an object of these lengths */
uint64_t object_pack_record_size(uint32_t name_len, uint64_t data_len);

/* Writes the record's first part, up to where the object goes, and returns
 * its length: the object's offset in the record. */
uint64_t object_pack_record_begin(uint8_t *dst, const char *name,
      uint32_t name_len, uint64_t data_len);

/* Writes what follows the object: its padding and the end mark. dst is
 * where the object ends. */
void object_pack_record_end(uint8_t *dst, uint32_t name_len,
      uint64_t data_len);

/* Reads the records of a file's image. Fills up to cap entries (name
 * pointing into the image) and returns the length of the file that counts;
 * *count receives how many records there are, also past cap. */
uint64_t object_pack_scan(const uint8_t *image, uint64_t len,
      struct object_pack_entry *entries, size_t cap, size_t *count);

void object_pack_index_init(struct object_pack_index *index);

/* Adds an entry whose name, offset and size are set. The entry and its name
 * stay where they are for as long as the index is used. An entry added later
 * is found before one of the same name added earlier. */
void object_pack_index_add(struct object_pack_index *index,
      struct object_pack_entry *entry);

const struct object_pack_entry *object_pack_index_find(
      const struct object_pack_index *index, const char *name,
      uint32_t name_len);

#ifdef __cplusplus
}
#endif

#endif
