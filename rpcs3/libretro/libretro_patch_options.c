/* RPCS3's game patches as core options (libretro_patch_options.h), the way
 * the cemu core lists a title's graphic packs: once the game is known, the
 * patches for its serial become options of their own in a last category,
 * Patch Manager, which is not there for a game without any (NNshi). C89.
 *
 * - Every patch is an Enabled/Disabled switch with its own notes, off unless
 *   turned on, as standalone lists them, one per description whichever
 *   executables (hashes) it is for, in the order of the descriptions.
 * - Patches that share a Group are switches too, of which one at most is on,
 *   as RPCS3 applies one patch of a group at most: turning one on turns the
 *   others off (NNshi: standalone does not merge them into one choice).
 * - A patch's configurable values with a list of allowed values are options
 *   too, shown while the patch is on. Ranges keep the patch's default.
 * - "All titles" patches (serial "All", hashed SPU programs) are listed only
 *   for the games they are known to affect (s_multigame); listed for every
 *   game they made the category appear everywhere. Turned on, they are on
 *   for every game, as in standalone - RPCS3 applies them only where the SPU
 *   program's hash matches anyway.
 *
 * Which are on is the .opt file's business: before each boot
 * patch_config.yml is written from the options, and RPCS3 applies a patch
 * only where the hash of the executable matches, so the version of the game
 * needs no matching here. */

#include "libretro_patch_options.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../Emu/Cell/lv2/game_text_patch.h"

#define NONE PATCH_DB_NONE

/* The "All titles" patches of patch.yml (hashed SPU programs) that name the
 * games they are known to affect only in their notes: those games' serials,
 * so the patch is listed for them. Demos and betas are left out. Made from
 * the notes with RPCS3's compatibility list (NNshi did the first). */
static const struct
{
   const char *description;
   const char *serials; /* separated by spaces */
} s_multigame[] = {
   { "Disable SPU MLAA - Prince of Persia series",
     "NPEB00341 NPEB00389 NPEB00390 NPUB30303 NPUB30387 NPUB30388" },
   { "Disable SPU MLAA - Multiple titles (01)",
     "BCAS20233 BCAS20281 BCES01503 BCES01740 BCUS98282 BCUS98377 BLES01211 BLES01222 BLES01250 BLES01414 BLJM60352 BLJM60489 BLJM61149 BLUS30732 BLUS30736 BLUS30842 NPEA00275 NPEA00288 NPEA00385 NPEA00386 NPEA00387 NPEA00438 NPEB00645 NPEB00821 NPEB00915 NPEB01363 NPEB02228 NPHA80188 NPJA00075 NPJB00466 NPJB00532 NPUA70218 NPUA80275 NPUA80523 NPUA80643 NPUA80644 NPUA80645 NPUB30745 NPUB31077 NPUB31345 NPUB31658" },
   { "Disable SPU MLAA - Multiple titles (02)",
     "BLES01066 BLES01167 BLES01169 BLES01399 BLJM60316 BLUS30580 BLUS30642 BLUS30709 BLUS30826 NPEB00618 NPEB02034 NPUB30539 NPUB31509" },
   { "Disable SPU MLAA - Multiple titles (03)",
     "BCAS20254 BCES01097 BCES01115 BCES01279 BCES01598 BCUS98259 BCUS98278 BCUS98279 NPEA00280 NPUA80677" },
   { "Disable SPU MLAA - Multiple titles (04)",
     "BLAS50796 BLES02178 BLES02243 BLJM61249 BLJM61313 BLUS31599 NPEB02281 NPUB31838 NPUB31857" },
   { "Disable SPU MLAA - WWE '13, WWE 2K14",
     "BLES01699 BLES01937 BLUS31015 BLUS31277 NPEB01815" },
   { "Disable SPU MLAA - ICO",
     "BCES01097 BCES01115 BCUS98259 NPEA00279 NPUA80676" },
   { "Disable SPU MLAA - Macross 30",
     "BLJS10184" },
   { "Disable SPU MLAA - Metro Last Light",
     "BLES01867 BLJS10218 BLUS31184 NPEB01369 NPUB31219" },
   { "Disable SPU MLAA - God of War: Ascension (01)",
     "BCES01741 BCES01742 BCUS98232 NPEA00445 NPHA80258 NPUA80918" },
   { "Disable SPU MLAA - God of War: Ascension (02)",
     "BCES01741 BCES01742 BCUS98232 NPEA00445 NPHA80258 NPUA80918" },
   { "Disable SPU MLAA - God of War: Ascension (03)",
     "BCES01741 BCES01742 BCUS98232 NPEA00445 NPHA80258 NPUA80918" },
   { "Disable SPU MLAA - Twisted Metal, Resident Evil: Operation Raccoon City",
     "BCES01010 BCES01400 BCUS98106 BLES01288 BLJM60342 BLUS30750 NPEA00362 NPEB00985 NPUA80079 NPUB30767" },
   { "Disable SPU MLAA - Multiple titles (05)",
     "BCES00484 BCES00956 BCES01104 BCES01284 BCES01285 BCES01369 BCUS98242 BCUS98247 BCUS98248 BCUS98271 BLES00991 BLES01392 BLES01393 BLES01698 BLES01796 BLES02026 BLJM60502 BLJS10127 BLUS30585 BLUS30613 BLUS30723 BLUS31433 NPEA00303 NPEA00304 NPEA00315 NPEA00333 NPEA00429 NPEA00505 NPEB00435 NPEB00476 NPEB01020 NPEB01332 NPJA00077 NPUA80619 NPUA80661 NPUA80678 NPUA80696 NPUA80875 NPUA80930 NPUB30383 NPUB30830 NPUB31072 NPUB31496" },
   { "Disable SPU MLAA - LittleBigPlanet 2 (01)",
     "BCAS20113 BCES00850 BCES01086 BCES01693 BCES01694 BCUS98245 BCUS98372 NPEA00324 NPEA00437 NPUA80662" },
   { "Disable SPU MLAA - LittleBigPlanet 2 (02)",
     "BCAS20113 BCES00850 BCES01086 BCES01693 BCES01694 BCUS98245 BCUS98372 NPEA00324 NPEA00437 NPUA80662" },
   { "Disable SPU MLAA - LittleBigPlanet 2, LittleBigPlanet 3, LittleBigPlanet Hub",
     "BCAS20113 BCES00850 BCES01086 BCES01663 BCES01693 BCES01694 BCES02068 BCUS98245 BCUS98362 BCUS98372 NPEA00324 NPEA00437 NPEA00515 NPUA80662 NPUA81116" },
};

/* The core's own patches beside RPCS3's: lines of a settings text built
 * into a game's executable, found by its bytes and rewritten in place as
 * the executable loads (game_text_patch.c), so every version of the game
 * that carries the text is patched. Listed for the games' serials. */
typedef struct text_edit
{
   const char *victim; /* a key the game does not read, whose line has room */
   const char *key;    /* the key it becomes */
   unsigned long value;
} text_edit_t;

typedef struct text_patch
{
   const char *name;
   const char *info;
   const char *serials; /* separated by spaces */
   const char *anchor;  /* bytes only the text holds */
   text_edit_t edits[2]; /* a null victim ends them */
} text_patch_t;

static const text_patch_t s_text_patches[] = {
   { "Reduce Audio Latency",
     "The sound engine mixes into a ring of 8 blocks of 256 samples instead of 16, refilled every 2 blocks instead of every 4: the engine's PS3MixerBlockSize and PS3MixerUpdateFrewuency, set in the sound setup built into the game's executable.",
     "BLUS30443 BLES00932 BCJS30022 BCAS20071 BLUD80018",
     "PS3MixerCHStreap8Num",
     { { "PS3MixerPriority", "PS3MixerBlockSize", 8 }, { "PS3MultiStreamSpuPriority", "PS3MixerUpdateFrewuency", 2 } } },
   /* Dark Souls: the same sound engine (MagicOrchestra over FMOD), with no
    * mixer keys in its setup text; what it has is how often the engine's
    * own thread runs. The line is rewritten as itself with the new value */
   { "Double Sound Update Rate",
     "The sound engine's thread takes the game's sound requests every 16.7 ms instead of every 33.3 ms, so a sound starts up to a frame sooner: the engine's MainThreadUpdateTime, set in the sound setup built into the game's executable.",
     "BLES01765 BLES01402 BLES01396 BLUS30782 BLJM60363",
     "FmodMultiChannnelDataPath",
     { { "MainThreadUpdateTime", "MainThreadUpdateTime", 16666 }, { NULL, NULL, 0 } } }
};

#define NUM_TEXT_PATCHES (sizeof(s_text_patches) / sizeof(s_text_patches[0]))
#define BLOCK_SIZE 16384

typedef struct text_option
{
   const char *key;
   const text_patch_t *patch;
} text_option_t;

/* A configurable value with allowed values: its labels and numbers */
typedef struct value_option
{
   const char *key;
   patch_str_t name;
   size_t first;        /* in labels and numbers */
   size_t count;
} value_option_t;

typedef struct patch_option
{
   const char *key;
   patch_str_t description;
   patch_str_t group;     /* empty: none; else one of a group is on at most */
   int all_titles;        /* an "All titles" patch, configured for all */
   size_t info;           /* the first patch with the description */
   size_t first_value;    /* in values */
   size_t num_values;
   int last_on;           /* what the switch was, to see it turned on */
} patch_option_t;

static struct
{
   const patch_db_t *db;
   const char *serial;
   text_option_t text[NUM_TEXT_PATCHES];
   size_t num_text;

   patch_option_t *options;
   size_t num_options, cap_options;
   value_option_t *values;
   size_t num_values, cap_values;
   const char **labels;
   size_t num_labels, cap_labels;
   double *numbers;
   size_t cap_numbers;

   struct retro_core_option_v2_definition *defs;
   size_t num_defs;

   patch_config_choice_t *choices;
   size_t num_choices, cap_choices;
   patch_config_pick_t *picks;
   size_t num_picks, cap_picks;
   size_t *choice_picks;  /* where each choice's picks start */
   size_t cap_choice_picks;

   char **blocks;
   size_t num_blocks, cap_blocks;
   char *block_pos;
   size_t block_left;

   patch_db_log_t log;
   void *log_user;
} s;

static int grow(void *arrp, size_t *cap, size_t need, size_t elem)
{
   void *old;
   void *moved;
   size_t n;
   if (need <= *cap)
      return 1;
   n = *cap ? *cap : 16;
   while (n < need)
      n *= 2;
   memcpy(&old, arrp, sizeof(old));
   moved = realloc(old, n * elem);
   if (!moved)
      return 0;
   memcpy(arrp, &moved, sizeof(moved));
   *cap = n;
   return 1;
}

static void say(int level, const char *msg)
{
   if (s.log)
      s.log(s.log_user, level, msg, strlen(msg));
}

void patch_options_set_log(patch_db_log_t log, void *user)
{
   s.log = log;
   s.log_user = user;
}

/* @len bytes that stay until the next collect, or NULL */
static char *keep(size_t len)
{
   char *p;
   if (len > s.block_left)
   {
      const size_t size = len > BLOCK_SIZE / 4 ? len : BLOCK_SIZE;
      char *block;
      if (!grow(&s.blocks, &s.cap_blocks, s.num_blocks + 1, sizeof(*s.blocks)))
         return NULL;
      block = (char*)malloc(size);
      if (!block)
         return NULL;
      s.blocks[s.num_blocks++] = block;
      if (size != BLOCK_SIZE)
         return block;
      s.block_pos = block;
      s.block_left = size;
   }
   p = s.block_pos;
   s.block_pos += len;
   s.block_left -= len;
   return p;
}

/* A string built of pieces, kept */
typedef struct text
{
   char *s;
   size_t n, cap;
} text_t;

static void put(text_t *t, const char *p, size_t len)
{
   if (!t->s && t->cap)
      return; /* failed before */
   /* A length whose sum wraps is refused like no memory: it would "fit"
    * in nothing at all */
   if (len >= (size_t)-1 - t->n || !grow(&t->s, &t->cap, t->n + len + 1, 1))
   {
      free(t->s);
      t->s = NULL;
      t->cap = 1;
      return;
   }
   memcpy(t->s + t->n, p, len);
   t->n += len;
   t->s[t->n] = '\0';
}

static void puts_(text_t *t, const char *p)
{
   put(t, p, strlen(p));
}

static void put_str(text_t *t, patch_str_t p)
{
   put(t, p.s, p.len);
}

/* The text, kept, and the builder emptied: "" when out of memory */
static const char *done(text_t *t)
{
   char *kept = t->s ? keep(t->n + 1) : NULL;
   if (kept)
      memcpy(kept, t->s, t->n + 1);
   free(t->s);
   t->s = NULL;
   t->n = t->cap = 0;
   return kept ? kept : "";
}

static void clear(void)
{
   size_t i;
   for (i = 0; i < s.num_blocks; i++)
      free(s.blocks[i]);
   s.num_blocks = 0;
   s.block_pos = NULL;
   s.block_left = 0;
   free(s.defs);
   s.defs = NULL;
   s.num_defs = 0;
   s.num_options = s.num_values = s.num_labels = 0;
   s.num_text = 0;
   s.num_choices = s.num_picks = 0;
   s.db = NULL;
   s.serial = "";
}

static int serial_listed(const char *serials, patch_str_t serial)
{
   const char *p = serials;
   while (*p)
   {
      const char *end = strchr(p, ' ');
      const size_t len = end ? (size_t)(end - p) : strlen(p);
      if (len == serial.len && memcmp(p, serial.s, len) == 0)
         return 1;
      if (!end)
         break;
      p = end + 1;
   }
   return 0;
}

/* Short and stable: the same patch of the same game gets the same key in
 * every build, so a choice in the .opt file stays with it */
static unsigned long fnv(unsigned long h, const char *p, size_t len)
{
   size_t i;
   for (i = 0; i < len; i++)
   {
      h ^= (unsigned char)p[i];
      h = (h * 0x01000193ul) & 0xfffffffful;
   }
   return h;
}

static void put_hash(text_t *t, unsigned long h)
{
   char hex[16];
   sprintf(hex, "%08lx", h);
   puts_(t, hex);
}

/* "rpcs3_zpatch_" and the hash of the serial, a separator, @kind, @name */
static const char *option_key(const char *kind, patch_str_t name)
{
   text_t t = { NULL, 0, 0 };
   unsigned long h = fnv(0x811c9dc5ul, s.serial, strlen(s.serial));
   h = fnv(h, "\x1f", 1);
   h = fnv(h, kind, strlen(kind));
   h = fnv(h, name.s, name.len);
   puts_(&t, "rpcs3_zpatch_");
   put_hash(&t, h);
   return done(&t);
}

static int str_eq(patch_str_t a, patch_str_t b)
{
   return a.len == b.len && (!a.len || memcmp(a.s, b.s, a.len) == 0);
}

static patch_str_t cstr(const char *p)
{
   patch_str_t out;
   out.s = p;
   out.len = strlen(p);
   return out;
}

static int for_serial(const patch_info_t *info, patch_str_t serial)
{
   size_t i;
   for (i = info->first_target; i < info->first_target + info->num_targets; i++)
      if (str_eq(s.db->targets[i].serial, serial))
         return 1;
   return 0;
}

/* An "All titles" patch whose notes name this game */
static int all_titles_for_serial(const patch_info_t *info, patch_str_t serial)
{
   size_t i;
   int all = 0;
   for (i = info->first_target; i < info->first_target + info->num_targets && !all; i++)
      all = patch_db_str_is(s.db->targets[i].title, "All") && patch_db_str_is(s.db->targets[i].serial, "All");
   if (!all)
      return 0;
   for (i = 0; i < sizeof(s_multigame) / sizeof(s_multigame[0]); i++)
      if (str_eq(info->description, cstr(s_multigame[i].description)))
         return serial_listed(s_multigame[i].serials, serial);
   return 0;
}

/* As std::string orders them: bytes, then length */
static int cmp_options(const void *a, const void *b)
{
   const patch_str_t x = ((const patch_option_t*)a)->description;
   const patch_str_t y = ((const patch_option_t*)b)->description;
   const int c = memcmp(x.s, y.s, x.len < y.len ? x.len : y.len);
   return c ? c : (x.len < y.len ? -1 : x.len > y.len);
}

/* The notes' first paragraph, as the sublabel */
static void put_first_paragraph(text_t *t, patch_str_t notes)
{
   size_t i;
   for (i = 0; i < notes.len; i++)
   {
      char c = notes.s[i];
      if (c == '\n' && i + 1 < notes.len && notes.s[i + 1] == '\n')
         break;
      if (c == '\n' || c == '\r')
         c = ' ';
      put(t, &c, 1);
   }
}

static void put_sep(text_t *t)
{
   if (t->n)
      puts_(t, " ");
}

/* The values a configurable value offers: its labels, or the numbers */
static int add_values(patch_option_t *o, const patch_info_t *info)
{
   const patch_db_t *db = s.db;
   size_t k, a;
   o->first_value = s.num_values;
   o->num_values = 0;
   for (k = info->first_config; k < info->first_config + info->num_configs; k++)
   {
      const patch_config_value_t *cv = &db->configs[k];
      value_option_t *v;
      text_t t = { NULL, 0, 0 };
      unsigned long h;
      if (!cv->num_allowed)
         continue; /* a range: the patch's default */
      if (!grow(&s.values, &s.cap_values, s.num_values + 1, sizeof(*s.values))
            || !grow(&s.labels, &s.cap_labels, s.num_labels + cv->num_allowed, sizeof(*s.labels))
            || !grow(&s.numbers, &s.cap_numbers, s.num_labels + cv->num_allowed, sizeof(*s.numbers)))
         return 0;
      v = &s.values[s.num_values++];
      o->num_values++;
      h = fnv(0x811c9dc5ul, o->description.s, o->description.len);
      h = fnv(h, "\x1f", 1);
      h = fnv(h, cv->name.s, cv->name.len);
      puts_(&t, o->key);
      puts_(&t, "_");
      put_hash(&t, h);
      v->key = done(&t);
      v->name = cv->name;
      v->first = s.num_labels;
      v->count = cv->num_allowed;
      for (a = cv->first_allowed; a < cv->first_allowed + cv->num_allowed; a++)
      {
         const patch_allowed_value_t *av = &db->allowed[a];
         if (av->label.len)
            put_str(&t, av->label);
         else
         {
            char num[64];
            sprintf(num, "%g", av->value);
            puts_(&t, num);
         }
         s.labels[s.num_labels] = done(&t);
         s.numbers[s.num_labels] = av->value;
         s.num_labels++;
      }
   }
   return 1;
}

static const char *kept_copy(patch_str_t p)
{
   text_t t = { NULL, 0, 0 };
   put_str(&t, p);
   if (!p.len)
      puts_(&t, "");
   return done(&t);
}

static void make_definitions(void)
{
   size_t n = s.num_text + s.num_options + s.num_values, i, d = 0;
   s.defs = (struct retro_core_option_v2_definition*)calloc(n ? n : 1, sizeof(*s.defs));
   if (!s.defs)
      return;

   /* The core's text patches, first */
   for (i = 0; i < s.num_text; i++)
   {
      struct retro_core_option_v2_definition *def = &s.defs[d++];
      text_t t = { NULL, 0, 0 };
      def->key = s.text[i].key;
      def->category_key = "patches";
      def->desc = s.text[i].patch->name;
      puts_(&t, s.text[i].patch->info);
      puts_(&t, " Applies at the next start of the game.");
      def->info = done(&t);
      def->values[0].value = "disabled";
      def->values[1].value = "enabled";
      def->default_value = "disabled";
   }

   for (i = 0; i < s.num_options; i++)
   {
      const patch_option_t *o = &s.options[i];
      const patch_info_t *first = &s.db->patches[o->info];
      struct retro_core_option_v2_definition *def = &s.defs[d++];
      text_t t = { NULL, 0, 0 };
      size_t v;

      def->key = o->key;
      def->category_key = "patches";
      put_first_paragraph(&t, first->notes);
      if (first->author.len)
      {
         put_sep(&t);
         puts_(&t, "(");
         put_str(&t, first->author);
         puts_(&t, ")");
      }
      if (o->group.len)
      {
         put_sep(&t);
         puts_(&t, "One patch of the group ");
         put_str(&t, o->group);
         puts_(&t, " at most is on.");
      }
      if (o->all_titles)
      {
         put_sep(&t);
         puts_(&t, "For several games: on, it is on for all of them.");
      }
      put_sep(&t);
      puts_(&t, "Applies at the next start of the game.");
      def->info = done(&t);
      def->desc = kept_copy(o->description);
      def->values[0].value = "disabled";
      def->values[1].value = "enabled";
      def->default_value = "disabled";

      for (v = o->first_value; v < o->first_value + o->num_values; v++)
      {
         const value_option_t *vo = &s.values[v];
         struct retro_core_option_v2_definition *vdef = &s.defs[d++];
         double default_value = 0;
         size_t k, index;
         for (k = first->first_config; k < first->first_config + first->num_configs; k++)
            if (str_eq(s.db->configs[k].name, vo->name))
               default_value = s.db->configs[k].value;
         vdef->key = vo->key;
         vdef->category_key = "patches";
         put_str(&t, o->description);
         puts_(&t, ": ");
         put_str(&t, vo->name);
         vdef->desc = done(&t);
         puts_(&t, "A value of the patch \"");
         put_str(&t, o->description);
         puts_(&t, "\".");
         vdef->info = done(&t);
         for (index = 0; index < vo->count && index + 1 < RETRO_NUM_CORE_OPTION_VALUES_MAX; index++)
         {
            vdef->values[index].value = s.labels[vo->first + index];
            if (s.numbers[vo->first + index] == default_value || !vdef->default_value)
               vdef->default_value = vdef->values[index].value;
         }
      }
   }
   s.num_defs = d;
}

void patch_options_collect(const patch_db_t *db, const char *serial)
{
   patch_str_t ser;
   size_t i, c, p;
   char msg[160];

   clear();
   game_text_patch_clear(); /* what the game before had on */
   if (!serial || !*serial)
      return;

   s.serial = kept_copy(cstr(serial));
   ser = cstr(s.serial);

   for (i = 0; i < NUM_TEXT_PATCHES; i++)
   {
      if (serial_listed(s_text_patches[i].serials, ser))
      {
         s.text[s.num_text].key = option_key("t", cstr(s_text_patches[i].name));
         s.text[s.num_text].patch = &s_text_patches[i];
         s.num_text++;
      }
   }

   s.db = db;
   if (db)
   {
      /* One option per description, whichever executables it is for */
      for (c = 0; c < db->num_containers; c++)
      {
         for (p = db->containers[c].first; p != NONE; p = db->patches[p].next)
         {
            const patch_info_t *info = &db->patches[p];
            const int on = for_serial(info, ser);
            const int all_titles = !on && all_titles_for_serial(info, ser);
            patch_option_t *o = NULL;
            if (!on && !all_titles)
               continue;
            for (i = 0; i < s.num_options && !o; i++)
               if (str_eq(s.options[i].description, info->description))
                  o = &s.options[i];
            if (!o)
            {
               if (!grow(&s.options, &s.cap_options, s.num_options + 1, sizeof(*s.options)))
                  break;
               o = &s.options[s.num_options++];
               memset(o, 0, sizeof(*o));
               o->description = info->description;
               o->info = p;
            }
            o->group = info->group;
            o->all_titles = o->all_titles || all_titles;
         }
      }

      if (s.num_options > 1)
         qsort(s.options, s.num_options, sizeof(*s.options), cmp_options);

      for (i = 0; i < s.num_options; i++)
      {
         patch_option_t *o = &s.options[i];
         o->key = option_key("p", o->description);
         if (!add_values(o, &db->patches[o->info]))
            break;
      }
   }

   make_definitions();

   sprintf(msg, "%lu patch options and %lu text patches for ", (unsigned long)s.num_options, (unsigned long)s.num_text);
   strncat(msg, s.serial, sizeof(msg) - strlen(msg) - 1);
   say(PATCH_LOG_NOTICE, msg);
}

int patch_options_any(void)
{
   return s.num_options || s.num_text;
}

const struct retro_core_option_v2_definition *patch_options_definitions(size_t *count)
{
   *count = s.defs ? s.num_defs : 0;
   return s.defs;
}

static int is_on(patch_options_get_t get, void *user, const char *key)
{
   const char *v = get(user, key);
   return v && strcmp(v, "enabled") == 0;
}

void patch_options_update_display(retro_environment_t cb, patch_options_get_t get, void *user)
{
   size_t i, j, v;
   if (!cb)
      return;

   /* A group's switch turned on turns the group's others off */
   for (i = 0; i < s.num_options; i++)
   {
      patch_option_t *o = &s.options[i];
      if (!o->group.len || !is_on(get, user, o->key) || o->last_on)
         continue;
      for (j = 0; j < s.num_options; j++)
      {
         patch_option_t *other = &s.options[j];
         struct retro_variable var;
         if (j == i || !str_eq(other->group, o->group) || !is_on(get, user, other->key))
            continue;
         var.key = other->key;
         var.value = "disabled";
         cb(RETRO_ENVIRONMENT_SET_VARIABLE, &var);
         other->last_on = 0;
      }
   }
   for (i = 0; i < s.num_options; i++)
      s.options[i].last_on = is_on(get, user, s.options[i].key);

   for (i = 0; i < s.num_options; i++)
   {
      const int on = is_on(get, user, s.options[i].key);
      for (v = s.options[i].first_value; v < s.options[i].first_value + s.options[i].num_values; v++)
      {
         struct retro_core_option_display display;
         display.key = s.values[v].key;
         display.visible = on;
         cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &display);
      }
   }
}

/* Whether @name has @word in it, whatever the case; @word in lower case */
static int name_has(patch_str_t name, const char *word)
{
   size_t i, j, n = strlen(word);
   for (i = 0; i + n <= name.len; i++)
   {
      for (j = 0; j < n; j++)
      {
         char c = name.s[i + j];
         if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
         if (c != word[j])
            break;
      }
      if (j == n)
         return 1;
   }
   return 0;
}

unsigned patch_options_frame_rate(patch_options_get_t get, void *user)
{
   size_t i, k, v;
   unsigned best = 0;

   for (i = 0; i < s.num_options; i++)
   {
      const patch_option_t *o = &s.options[i];
      if (!is_on(get, user, o->key))
         continue;
      for (v = o->first_value; v < o->first_value + o->num_values; v++)
      {
         const value_option_t *vo = &s.values[v];
         const char *label = get(user, vo->key);
         if (!label || (!name_has(vo->name, "fps") && !name_has(vo->name, "frame rate")
               && !name_has(vo->name, "framerate")))
            continue;
         for (k = vo->first; k < vo->first + vo->count; k++)
         {
            const char *c = label;
            unsigned rate = 0;
            if (strcmp(s.labels[k], label) != 0)
               continue;
            /* The label's first number, else the value when it is a rate */
            while (*c && (*c < '0' || *c > '9'))
               c++;
            if (*c)
            {
               while (*c >= '0' && *c <= '9' && rate < 100000)
                  rate = rate * 10 + (unsigned)(*c++ - '0');
            }
            else if (s.numbers[k] >= 20.0 && s.numbers[k] <= 1000.0)
               rate = (unsigned)s.numbers[k];
            if (rate >= 20 && rate <= 1000 && rate > best)
               best = rate;
            break;
         }
      }
   }
   return best;
}

const patch_config_choice_t *patch_options_choices(patch_options_get_t get, void *user, size_t *count)
{
   size_t i, j, k, v;

   /* The core's text patches turned on, for the PPU loader */
   game_text_patch_clear();
   for (i = 0; i < s.num_text; i++)
   {
      const text_patch_t *patch = s.text[i].patch;
      if (!is_on(get, user, s.text[i].key))
         continue;
      for (j = 0; j < sizeof(patch->edits) / sizeof(patch->edits[0]); j++)
      {
         const text_edit_t *edit = &patch->edits[j];
         if (edit->victim && !game_text_patch_add(patch->anchor, edit->victim, edit->key, edit->value))
         {
            char msg[160];
            sprintf(msg, "%.60s: no room for the edit of %.60s", patch->name, edit->key);
            say(PATCH_LOG_ERROR, msg);
         }
      }
   }

   /* The whole file from the options: what standalone or an earlier game
    * left in it would otherwise stay on */
   s.num_choices = s.num_picks = 0;
   for (i = 0; i < s.num_options; i++)
   {
      const patch_option_t *o = &s.options[i];
      patch_config_choice_t *choice;
      int taken = 0;
      if (!is_on(get, user, o->key))
         continue;
      /* One of a group at most, should the .opt file have more on */
      for (j = 0; j < s.num_choices && o->group.len && !taken; j++)
         taken = str_eq(s.options[s.choice_picks[j * 2 + 1]].group, o->group);
      if (taken)
      {
         text_t t = { NULL, 0, 0 };
         put_str(&t, o->description);
         puts_(&t, ": another patch of group ");
         put_str(&t, o->group);
         puts_(&t, " is on already");
         say(PATCH_LOG_WARNING, done(&t));
         continue;
      }
      if (!grow(&s.choices, &s.cap_choices, s.num_choices + 1, sizeof(*s.choices))
            || !grow(&s.choice_picks, &s.cap_choice_picks, (s.num_choices + 1) * 2, sizeof(*s.choice_picks)))
         break;

      /* The values picked, by name */
      s.choice_picks[s.num_choices * 2] = s.num_picks;
      s.choice_picks[s.num_choices * 2 + 1] = i;
      for (v = o->first_value; v < o->first_value + o->num_values; v++)
      {
         const value_option_t *vo = &s.values[v];
         const char *label = get(user, vo->key);
         for (k = vo->first; k < vo->first + vo->count && label; k++)
         {
            if (strcmp(s.labels[k], label) != 0)
               continue;
            if (!grow(&s.picks, &s.cap_picks, s.num_picks + 1, sizeof(*s.picks)))
               break;
            s.picks[s.num_picks].name = vo->name.s;
            s.picks[s.num_picks].name_len = vo->name.len;
            s.picks[s.num_picks].value = s.numbers[k];
            s.num_picks++;
         }
      }

      /* An "All titles" patch is configured for all, as standalone does;
       * RPCS3 matches its hash itself */
      choice = &s.choices[s.num_choices++];
      choice->description = o->description.s;
      choice->description_len = o->description.len;
      choice->serial_key = o->all_titles ? "All" : s.serial;
   }

   /* The picks, now that they no longer move */
   for (i = 0; i < s.num_choices; i++)
   {
      const size_t first = s.choice_picks[i * 2];
      const size_t end = i + 1 < s.num_choices ? s.choice_picks[i * 2 + 2] : s.num_picks;
      s.choices[i].picks = s.picks ? s.picks + first : NULL;
      s.choices[i].num_picks = end - first;
   }

   *count = s.num_choices;
   return s.choices;
}
