/* RPCS3's per-game settings database: see config_database.h. C89.
 *
 * A string rjson hands over lives until its next event, so nothing is kept
 * of a member name but whether it is the one wanted at its depth: "games"
 * at the top, the title id inside it, "config" inside that. */

#include <stdlib.h>
#include <string.h>

#include <formats/rjson.h>
#include <compat/strl.h>

#include "config_database.h"

/* The depths of the members on the way down: games, title id, config */
#define DB_DEPTHS 3

char *config_database_lookup(const char *json, size_t len, const char *title_id,
      char *error, size_t error_size)
{
   rjson_t *r;
   char *out           = NULL;
   unsigned depth      = 0;
   size_t title_len    = strlen(title_id);
   int wanted[DB_DEPTHS + 1];

   if (error_size)
      *error = '\0';
   if (!title_len)
      return NULL;
   if (!(r = rjson_open_buffer(json, len)))
      return NULL;
   rjson_set_options(r, RJSON_OPTION_ALLOW_UTF8BOM);
   memset(wanted, 0, sizeof(wanted));

   for (;;)
   {
      enum rjson_type t = rjson_next(r);

      if (t == RJSON_DONE)
         break;
      if (t == RJSON_ERROR)
      {
         if (error_size)
            strlcpy(error, rjson_get_error(r), error_size);
         break;
      }

      if (t == RJSON_OBJECT || t == RJSON_ARRAY)
      {
         depth++;
         if (depth <= DB_DEPTHS)
            wanted[depth] = 0;
      }
      else if (t == RJSON_OBJECT_END || t == RJSON_ARRAY_END)
         depth--;
      else if (t == RJSON_STRING && depth >= 1 && depth <= DB_DEPTHS)
      {
         size_t n;
         const char *s = rjson_get_string(r, &n);

         if (rjson_get_context_type(r) == RJSON_OBJECT
               && (rjson_get_context_count(r) & 1))
         {
            /* A member name */
            if (depth == 1)
               wanted[1] = n == 5 && !memcmp(s, "games", 5);
            else if (depth == 2)
               wanted[2] = n == title_len && !memcmp(s, title_id, n);
            else
               wanted[3] = n == 6 && !memcmp(s, "config", 6);
         }
         else if (depth == DB_DEPTHS && wanted[1] && wanted[2] && wanted[3])
         {
            if ((out = (char*)malloc(n + 1)))
            {
               memcpy(out, s, n);
               out[n] = '\0';
            }
            break;
         }
      }
   }

   rjson_free(r);
   return out;
}
