/* RPCS3's per-game settings database (libretro/config_database.c), the
 * settings the core puts on top of the user's for a game:
 *
 * - every entry of the database the core carries (config_database.dat),
 *   looked up by its title id, gives that entry's settings: the list is
 *   made by a second reader that copies each name it passes, against the
 *   lookup that keeps none (the core's last lookup kept rjson's strings
 *   past their life and found the first entries only; Demon's Souls lost
 *   its Write Color Buffers and drew its HUD on black)
 * - a title id that is not there, one that is a prefix of another, and
 *   "games" or "config" met elsewhere in the file find nothing
 * - a byte order mark is read past; a file that is not JSON gives its error
 *
 * Usage: test_config_database config_database.dat */

#include <formats/rjson.h>
#include <compat/strl.h>

#include "../config_database.h"
#include "test_common.h"

/* The second reader: every games -> <id> -> config, names copied */
struct entries
{
   char key[4][64];
   unsigned depth;
   size_t count;
   size_t failed;
   const char *json;
   size_t json_len;
};

static bool on_member(void *ctx, const char *s, size_t n)
{
   struct entries *e = (struct entries*)ctx;
   if (e->depth < 4)
   {
      if (n >= sizeof(e->key[0]))
         n = sizeof(e->key[0]) - 1;
      memcpy(e->key[e->depth], s, n);
      e->key[e->depth][n] = '\0';
   }
   return true;
}

static bool on_string(void *ctx, const char *s, size_t n)
{
   struct entries *e = (struct entries*)ctx;
   if (e->depth == 3 && !strcmp(e->key[1], "games") && !strcmp(e->key[3], "config"))
   {
      char err[64];
      char *got = config_database_lookup(e->json, e->json_len, e->key[2], err, sizeof(err));
      e->count++;
      if (!got || strlen(got) != n || memcmp(got, s, n))
      {
         if (e->failed++ < 5)
            fprintf(stderr, "%s: %s\n", e->key[2], got ? "other settings" : "not found");
      }
      free(got);
   }
   return true;
}

static bool on_open(void *ctx)
{
   struct entries *e = (struct entries*)ctx;
   e->depth++;
   if (e->depth < 4)
      e->key[e->depth][0] = '\0';
   return true;
}

static bool on_close(void *ctx)
{
   ((struct entries*)ctx)->depth--;
   return true;
}

static char *find(const char *json, const char *id)
{
   char err[64];
   return config_database_lookup(json, strlen(json), id, err, sizeof(err));
}

static int found_as(const char *json, const char *id, const char *want)
{
   char *got = find(json, id);
   int ok = want ? got && !strcmp(got, want) : !got;
   free(got);
   return ok;
}

int main(int argc, char **argv)
{
   static const char doc[] =
      "\xEF\xBB\xBF{\"return_code\": 0, \"config\": \"top\",\n"
      " \"other\": {\"BLUS30443\": {\"config\": \"elsewhere\"}},\n"
      " \"games\": {\n"
      "  \"BLUS3044\": {\"config\": \"prefix\", \"notes\": [\"config\", {\"config\": \"deep\"}]},\n"
      "  \"BLUS30443\": {\"other\": {\"config\": \"nested\"}, \"config\": \"Video:\\n  Write Color Buffers: true\"},\n"
      "  \"BLES00932\": {\"config\": \"x\"}\n"
      " },\n"
      " \"BLES00999\": {\"config\": \"outside\"}\n"
      "}";
   struct entries e;
   uint8_t *data;
   size_t len = 0;
   char err[64];
   char *got;

   TEST_CHECK(found_as(doc, "BLUS30443", "Video:\n  Write Color Buffers: true"));
   TEST_CHECK(found_as(doc, "BLUS3044", "prefix"));
   TEST_CHECK(found_as(doc, "BLES00932", "x"));
   TEST_CHECK(found_as(doc, "BLUS304", NULL));
   TEST_CHECK(found_as(doc, "BLES00999", NULL));
   TEST_CHECK(found_as(doc, "", NULL));

   got = config_database_lookup("{\"games\": [", 11, "BLUS30443", err, sizeof(err));
   TEST_CHECK(!got && err[0]);
   got = config_database_lookup(doc, sizeof(doc) - 1, "BLES00999", err, sizeof(err));
   TEST_CHECK(!got && !err[0]);

   if (argc < 2 || !(data = test_read_file(argv[1], &len)))
   {
      fprintf(stderr, "cannot read the database\n");
      return 1;
   }

   memset(&e, 0, sizeof(e));
   e.json     = (const char*)data;
   e.json_len = len;
   TEST_CHECK(rjson_parse_quick(e.json, len, &e, RJSON_OPTION_ALLOW_UTF8BOM,
         on_member, on_string, NULL, on_open, on_close, on_open, on_close,
         NULL, NULL, NULL));
   TEST_CHECK(e.count > 1000);
   TEST_CHECK(e.failed == 0);
   printf("config database: %u entries, %u not found as they are\n",
         (unsigned)e.count, (unsigned)e.failed);
   free(data);

   if (test_failures)
      printf("config database: FAIL\n");
   return test_failures ? 1 : 0;
}
