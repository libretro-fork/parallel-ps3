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
 * The index is added to and read by any number of threads with no lock, and
 * so is an open pack: found in, read from and added to.
 *
 * A pack is read once, whole, when it is opened. What was in it then can be
 * borrowed from that block (object_pack_lent) for as long as the block is
 * held: the pack holds it until it is closed, and a borrower that outlives
 * the pack takes its own hold (object_pack_image_take). */

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

/* The file a pack is in. read_at and write_at return the bytes done and do
 * not use a position of the file's. */
struct object_pack_io
{
   void *file;
   uint64_t (*size)(void *file);
   uint64_t (*read_at)(void *file, uint64_t offset, void *buf, uint64_t len);
   uint64_t (*write_at)(void *file, uint64_t offset, const void *buf,
         uint64_t len);
};

struct object_pack
{
   struct object_pack_io io;
   retro_atomic_64_t end;             /* where the next record goes */
   uint8_t *image;                    /* the file as read, or NULL */
   uint64_t lent_len;                 /* how much of it counts */
   struct object_pack_entry *scanned; /* its records */
   size_t scanned_count;
   struct object_pack_index index;
};

/* Reads the file and indexes its records. io may be NULL, or its file: the
 * pack is then empty and nothing can be added. A file that is not a pack is
 * started over. */
void object_pack_open(struct object_pack *pack,
      const struct object_pack_io *io);

/* Frees the index and lets go of the image. Not while another thread uses
 * the pack. */
void object_pack_close(struct object_pack *pack);

const struct object_pack_entry *object_pack_find(
      const struct object_pack *pack, const char *name, uint32_t name_len);

/* Writes an object at the pack's end - the record's start, the object from
 * where it is, the end mark - and indexes it. NULL when it could not be
 * written. name_len is at most 240. */
const struct object_pack_entry *object_pack_add(struct object_pack *pack,
      const char *name, uint32_t name_len, const void *data, uint64_t size);

/* The object where it lies in the image, or NULL for one added since the
 * pack was opened (object_pack_read). */
const uint8_t *object_pack_lent(const struct object_pack *pack,
      const struct object_pack_entry *entry);

/* Reads the object from the file. 1 when all of it was read. */
int object_pack_read(const struct object_pack *pack,
      const struct object_pack_entry *entry, void *dst);

/* Calls fn for every object, once a name (the one a find gives) */
typedef void (*object_pack_each_t)(void *ud,
      const struct object_pack_entry *entry);
void object_pack_each(const struct object_pack *pack, object_pack_each_t fn,
      void *ud);

/* A hold on the image for a borrower that may outlive the pack, and its
 * release; the image is freed with the last hold. */
void object_pack_image_take(uint8_t *image);
void object_pack_image_drop(uint8_t *image);

#ifdef __cplusplus
}

/* The callbacks for one of the core's files (fs::file), which stays where it
 * is for as long as the pack is open */
template <typename File>
inline object_pack_io object_pack_io_of(File& file)
{
   object_pack_io io;
   io.file     = &file;
   io.size     = [](void* f) -> uint64_t { return static_cast<File*>(f)->size(); };
   io.read_at  = [](void* f, uint64_t offset, void* buf, uint64_t len) -> uint64_t { return static_cast<File*>(f)->read_at(offset, buf, len); };
   io.write_at = [](void* f, uint64_t offset, const void* buf, uint64_t len) -> uint64_t { return static_cast<File*>(f)->write_at(offset, buf, len); };
   return io;
}
#endif

#endif
