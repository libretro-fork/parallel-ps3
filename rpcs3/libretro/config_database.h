/* RPCS3's per-game settings database (config_database.dat, the JSON
 * standalone downloads): the "config" string of games -> <title id>, read
 * with libretro-common's rjson. C89. */

#ifndef LIBRETRO_CONFIG_DATABASE_H
#define LIBRETRO_CONFIG_DATABASE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The entry for @title_id in @json (@len bytes), as RPCS3's YAML config,
 * malloc'd and NUL-terminated; NULL when there is none. When @json is not
 * JSON its error goes into @error (@error_size bytes, may be 0), "" otherwise. */
char *config_database_lookup(const char *json, size_t len, const char *title_id,
      char *error, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
