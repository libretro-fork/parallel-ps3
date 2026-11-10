/* The yaml-cpp replacement: libretro-common's ryaml, read as the core reads
 * its YAML, against what yaml-cpp (RPCS3's, 51a5d623) made of the same
 * documents:
 *
 * - an excerpt of RPCS3's patch.yml (Version, the Anchors section, its
 *   "load" entries by alias, values on the line below their keys, \x
 *   escapes, comments everywhere), each entry's tree against yaml-cpp's;
 * - 300 random documents written by yaml-cpp's emitter, as the config,
 *   patch config, games.yml and the rest were written before, each tree
 *   against yaml-cpp's reading of it;
 * - and every one of those trees written again with ryaml_writer, as the
 *   core now writes them, and read back to the same tree.
 *
 * The trees are compared by a hash of a canonical form (yaml_trees.txt,
 * made by yaml_fixtures_gen.cpp against yaml-cpp), one per patch entry and
 * per document, so a difference names its place.
 *
 * Usage: test_yaml fixtures-dir */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <encodings/deflate.h>
#include <formats/ryaml.h>
#include <formats/ryaml_writer.h>

static int test_failures = 0;

#define TEST_CHECK(cond) do { if (!(cond)) { \
   fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
   test_failures++; } } while (0)

/* ---- canonical form, as yaml_fixtures_gen.cpp builds it ---- */

struct buf
{
   char  *p;
   size_t len, cap;
};

static void put(struct buf *b, const char *s, size_t n)
{
   if (b->len + n > b->cap)
   {
      size_t cap = b->cap ? b->cap * 2 : 4096;
      while (cap < b->len + n)
         cap *= 2;
      b->p   = (char*)realloc(b->p, cap);
      b->cap = cap;
   }
   memcpy(b->p + b->len, s, n);
   b->len += n;
}

static void put_counted(struct buf *b, char tag, const char *s, size_t n)
{
   char head[32];
   sprintf(head, "%c%u:", tag, (unsigned)n);
   put(b, head, strlen(head));
   put(b, s, n);
}

static void canon(const ryaml_t *y, int n, struct buf *b)
{
   int c;
   switch (ryaml_type(y, n))
   {
      case RYAML_TYPE_MAP:
         put(b, "{", 1);
         for (c = ryaml_first_child(y, n); c >= 0; c = ryaml_next_sibling(y, c))
         {
            size_t      kl;
            const char *k = ryaml_key(y, c, &kl);
            put_counted(b, 'K', k ? k : "", kl);
            canon(y, c, b);
         }
         put(b, "}", 1);
         break;
      case RYAML_TYPE_SEQ:
         put(b, "[", 1);
         for (c = ryaml_first_child(y, n); c >= 0; c = ryaml_next_sibling(y, c))
            canon(y, c, b);
         put(b, "]", 1);
         break;
      default:
         if (ryaml_has_val(y, n))
         {
            size_t      vl;
            const char *v = ryaml_val(y, n, &vl);
            put_counted(b, 'S', v, vl);
         }
         else
            put(b, "N", 1);
         break;
   }
}

static uint64_t fnv(const char *s, size_t n)
{
   uint64_t h = 1469598103934665603ull;
   size_t   i;
   for (i = 0; i < n; i++)
   {
      h ^= (unsigned char)s[i];
      h *= 1099511628211ull;
   }
   return h;
}

/* One entry: its key, then its tree */
static uint64_t entry_hash(const ryaml_t *y, int n)
{
   struct buf  b = { NULL, 0, 0 };
   size_t      kl;
   const char *k = ryaml_key(y, n, &kl);
   uint64_t    h;
   put_counted(&b, 'K', k ? k : "", kl);
   canon(y, n, &b);
   h = fnv(b.p, b.len);
   free(b.p);
   return h;
}

static uint64_t tree_hash(const ryaml_t *y)
{
   struct buf b = { NULL, 0, 0 };
   uint64_t   h;
   canon(y, ryaml_root(y), &b);
   h = fnv(b.p, b.len);
   free(b.p);
   return h;
}

/* ---- writing a tree again, as the core writes ---- */

static void rewrite(ryaml_writer_t *w, const ryaml_t *y, int n)
{
   int c;
   switch (ryaml_type(y, n))
   {
      case RYAML_TYPE_MAP:
         ryaml_writer_begin_map(w);
         for (c = ryaml_first_child(y, n); c >= 0; c = ryaml_next_sibling(y, c))
         {
            size_t      kl;
            const char *k = ryaml_key(y, c, &kl);
            ryaml_writer_key(w, k ? k : "", kl);
            rewrite(w, y, c);
         }
         ryaml_writer_end_map(w);
         break;
      case RYAML_TYPE_SEQ:
         ryaml_writer_begin_seq(w, 0);
         for (c = ryaml_first_child(y, n); c >= 0; c = ryaml_next_sibling(y, c))
            rewrite(w, y, c);
         ryaml_writer_end_seq(w);
         break;
      default:
      {
         size_t      vl;
         const char *v = ryaml_val(y, n, &vl);
         ryaml_writer_scalar(w, v ? v : "", vl);
         break;
      }
   }
}

/* ---- fixtures ---- */

static char *gunzip(const char *path, size_t *out_len)
{
   size_t   in_len = 0, cap = 1 << 20, taken = 0, wrote = 0;
   uint8_t *in;
   char    *out;
   void    *z;
   int      r;
   FILE    *f = fopen(path, "rb");
   if (!f)
      return NULL;
   fseek(f, 0, SEEK_END);
   in_len = (size_t)ftell(f);
   fseek(f, 0, SEEK_SET);
   in = (uint8_t*)malloc(in_len);
   if (fread(in, 1, in_len, f) != in_len)
      in_len = 0;
   fclose(f);
   out = (char*)malloc(cap);
   z   = rinflate_new(16 + 15);
   rinflate_set_in(z, in, in_len);
   rinflate_set_out(z, (uint8_t*)out, cap);
   r = rinflate_process(z, &taken, &wrote);
   rinflate_free(z);
   free(in);
   if (r != RDEFLATE_PROCESS_END)
   {
      free(out);
      return NULL;
   }
   *out_len = wrote;
   return out;
}

static uint64_t expected[400];
static int      n_patch, n_doc;

static int read_trees(const char *path)
{
   char  line[128];
   FILE *f = fopen(path, "r");
   if (!f)
      return 0;
   while (fgets(line, sizeof(line), f))
   {
      char               kind[16];
      unsigned           i;
      unsigned long long h;
      if (sscanf(line, "%15s %u %llx", kind, &i, &h) != 3 || i >= 300)
         continue;
      if (!strcmp(kind, "patch"))
      {
         expected[i] = (uint64_t)h;
         n_patch     = (int)i + 1;
      }
      else
      {
         expected[100 + i] = (uint64_t)h;
         n_doc = (int)i + 1;
      }
   }
   fclose(f);
   return 1;
}

int main(int argc, char **argv)
{
   char   path[1024];
   char  *text;
   size_t len;
   int    i;

   if (argc < 2)
      return 2;

   sprintf(path, "%s/yaml_trees.txt", argv[1]);
   TEST_CHECK(read_trees(path) && n_patch > 20 && n_doc == 300);

   /* the patch file: each entry of each top-level mapping */
   sprintf(path, "%s/patch_excerpt.yml.gz", argv[1]);
   text = gunzip(path, &len);
   TEST_CHECK(text != NULL);
   if (text)
   {
      size_t   line = 0, col = 0;
      ryaml_t *y    = ryaml_parse_ex(text, len, &line, &col);
      int      top, e, k = 0, bad = 0;
      if (!y)
         fprintf(stderr, "patch excerpt refused at line %u, column %u\n", (unsigned)line, (unsigned)col);
      TEST_CHECK(y != NULL);
      for (top = ryaml_first_child(y, ryaml_root(y)); y && top >= 0; top = ryaml_next_sibling(y, top))
      {
         if (!ryaml_is_map(y, top))
         {
            if (entry_hash(y, top) != expected[k])
               bad++;
            k++;
            continue;
         }
         for (e = ryaml_first_child(y, top); e >= 0; e = ryaml_next_sibling(y, e), k++)
         {
            if (entry_hash(y, e) != expected[k])
            {
               size_t      kl;
               const char *key = ryaml_key(y, e, &kl);
               fprintf(stderr, "patch entry %d (%.*s) differs from yaml-cpp\n", k, (int)kl, key);
               bad++;
            }
         }
      }
      TEST_CHECK(k == n_patch);
      TEST_CHECK(bad == 0);
      printf("test_yaml: patch.yml excerpt, %d entries as yaml-cpp reads them\n", k);
      ryaml_free(y);
      free(text);
   }

   /* yaml-cpp's own output, read; then written again and read back */
   sprintf(path, "%s/yaml_cpp_docs.yml.gz", argv[1]);
   text = gunzip(path, &len);
   TEST_CHECK(text != NULL);
   if (text)
   {
      char *doc = text, *end = text + len;
      int   bad = 0, bad_rw = 0;
      for (i = 0; i < n_doc && doc < end; i++)
      {
         char    *sep = strstr(doc, "\n...\n");
         size_t   dlen;
         ryaml_t *y;
         if (!sep)
            break;
         dlen = (size_t)(sep - doc) + 1;
         y    = ryaml_parse(doc, dlen);
         if (!y || tree_hash(y) != expected[100 + i])
         {
            fprintf(stderr, "document %d differs from yaml-cpp:\n%.*s\n", i, (int)dlen, doc);
            bad++;
         }
         else
         {
            ryaml_writer_t *w = ryaml_writer_new(NULL, NULL);
            const char     *out;
            size_t          olen;
            ryaml_t        *y2;
            rewrite(w, y, ryaml_root(y));
            out = ryaml_writer_memory(w, &olen);
            y2  = out ? ryaml_parse(out, olen) : NULL;
            if (!y2 || tree_hash(y2) != expected[100 + i])
            {
               fprintf(stderr, "document %d, written again, reads back differently:\n%s\n", i, out ? out : "");
               bad_rw++;
            }
            ryaml_free(y2);
            ryaml_writer_free(w);
         }
         ryaml_free(y);
         doc = sep + 5;
      }
      TEST_CHECK(i == n_doc);
      TEST_CHECK(bad == 0);
      TEST_CHECK(bad_rw == 0);
      printf("test_yaml: %d documents yaml-cpp wrote, read as it reads them, and written again the same\n", i);
      free(text);
   }

   if (test_failures)
   {
      fprintf(stderr, "test_yaml: %d check(s) failed\n", test_failures);
      return 1;
   }
   printf("test_yaml: ok\n");
   return 0;
}
