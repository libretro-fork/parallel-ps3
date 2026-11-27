/* The Patch Manager's options (libretro_patch_options.c): a game's patches
 * listed once per description in order, with their notes, author and group
 * in the sublabel, "All titles" patches only for the games named for them,
 * the core's text patches first, value choices for configurable values,
 * keys that stay the same; a group's switch turned on turning the others
 * off, value choices shown with their patch; and patch_config.yml's choices
 * with the values picked. C89. */

#include "test_common.h"

#include "../libretro_patch_options.h"
#include "../../Emu/Cell/lv2/game_text_patch.h"

#include <formats/ryaml_writer.h>

static const char s_file[] =
   "Version: 1.2\n"
   "PPU-a:\n"
   "  Zeta:\n"
   "    Group: g\n"
   "    Author: me\n"
   "    Notes: \"First para\\nline two\\n\\nSecond\"\n"
   "    Games:\n"
   "      Game A:\n"
   "        BLUS30443: [ 01.00 ]\n"
   "    Patch:\n"
   "      - [ byte, 0x0, 1 ]\n"
   "  Alpha:\n"
   "    Group: g\n"
   "    Games:\n"
   "      Game A:\n"
   "        BLUS30443: [ All ]\n"
   "    Configurable Values:\n"
   "      Mode:\n"
   "        Type: long_enum\n"
   "        Value: 2\n"
   "        Allowed Values:\n"
   "          Low: 1\n"
   "          High: 2\n"
   "    Patch:\n"
   "      - [ byte, 0x0, Mode ]\n"
   "  Other game:\n"
   "    Games:\n"
   "      Game B:\n"
   "        BLES00001: [ 01.00 ]\n"
   "    Patch:\n"
   "      - [ byte, 0x0, 1 ]\n"
   "SPU-x:\n"
   "  Disable SPU MLAA - ICO:\n"
   "    Games:\n"
   "      All:\n"
   "        All: [ All ]\n"
   "    Patch:\n"
   "      - [ be32, 0x0, 1 ]\n"
   "PPU-b:\n"
   "  Alpha:\n"
   "    Group: g\n"
   "    Games:\n"
   "      Game A:\n"
   "        BLUS30443: [ 01.01 ]\n"
   "    Configurable Values:\n"
   "      Mode:\n"
   "        Type: long_enum\n"
   "        Value: 2\n"
   "        Allowed Values:\n"
   "          Low: 1\n"
   "          High: 2\n"
   "    Patch:\n"
   "      - [ byte, 0x1, Mode ]\n"
   "  Speed:\n"
   "    Games:\n"
   "      Game C:\n"
   "        BLES00009: [ 01.00 ]\n"
   "    Configurable Values:\n"
   "      Frame Rate:\n"
   "        Type: double_enum\n"
   "        Value: 0.016666668\n"
   "        Allowed Values:\n"
   "          \"60 FPS\": 0.016666668\n"
   "          \"120 FPS (Vblank Rate 120)\": 0.008333334\n"
   "    Patch:\n"
   "      - [ bef32, 0x0, Frame Rate ]\n"
   "  Cap:\n"
   "    Games:\n"
   "      Game C:\n"
   "        BLES00009: [ 01.00 ]\n"
   "    Configurable Values:\n"
   "      Target FPS:\n"
   "        Type: long_enum\n"
   "        Value: 30\n"
   "        Allowed Values:\n"
   "          Half: 30\n"
   "          Full: 90\n"
   "      Mode:\n"
   "        Type: long_enum\n"
   "        Value: 240\n"
   "        Allowed Values:\n"
   "          Odd: 240\n"
   "          Even: 241\n"
   "    Patch:\n"
   "      - [ byte, 0x0, Target FPS ]\n"
   "      - [ byte, 0x1, Mode ]\n";

/* The frontend's values */
static struct
{
   char key[64];
   char value[32];
} s_vars[16];
static unsigned s_num_vars, s_sets, s_shown, s_hidden;

static const char *get(void *user, const char *key)
{
   unsigned i;
   (void)user;
   for (i = 0; i < s_num_vars; i++)
      if (strcmp(s_vars[i].key, key) == 0)
         return s_vars[i].value;
   return "disabled";
}

static void set(const char *key, const char *value)
{
   unsigned i;
   for (i = 0; i < s_num_vars; i++)
      if (strcmp(s_vars[i].key, key) == 0)
         break;
   if (i == s_num_vars)
   {
      if (s_num_vars == 16)
         return;
      s_num_vars++;
      strcpy(s_vars[i].key, key);
   }
   strcpy(s_vars[i].value, value);
}

static bool env(unsigned cmd, void *data)
{
   if (cmd == RETRO_ENVIRONMENT_SET_VARIABLE)
   {
      const struct retro_variable *var = (const struct retro_variable*)data;
      set(var->key, var->value);
      s_sets++;
   }
   else if (cmd == RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY)
   {
      const struct retro_core_option_display *d = (const struct retro_core_option_display*)data;
      if (d->visible)
         s_shown++;
      else
         s_hidden++;
   }
   return true;
}

static void expect_key(const char *key, const char *text, size_t len)
{
   unsigned long h = 0x811c9dc5ul;
   char want[32];
   size_t i;
   for (i = 0; i < len; i++)
   {
      h ^= (unsigned char)text[i];
      h = (h * 0x01000193ul) & 0xfffffffful;
   }
   sprintf(want, "rpcs3_zpatch_%08lx", h);
   TEST_CHECK(strcmp(key, want) == 0);
}

static unsigned s_warnings;

static void log_fn(void *user, int level, const char *msg, size_t len)
{
   (void)user;
   (void)msg;
   (void)len;
   if ((level & PATCH_LOG_LEVEL_MASK) == PATCH_LOG_WARNING)
      s_warnings++;
}

int main(void)
{
   patch_db_t db;
   const struct retro_core_option_v2_definition *defs;
   const patch_config_choice_t *choices;
   size_t count = 0;
   char *text = (char*)malloc(sizeof(s_file));

   memcpy(text, s_file, sizeof(s_file) - 1);
   patch_db_init(&db, NULL, NULL);
   TEST_CHECK(patch_db_load(&db, "t.yml", text, sizeof(s_file) - 1, NULL, 0));
   patch_options_set_log(log_fn, NULL);

   /* A game with a text patch, a group and a configurable value */
   patch_options_collect(&db, "BLUS30443");
   TEST_CHECK(patch_options_any());
   defs = patch_options_definitions(&count);
   TEST_CHECK(count == 4);
   if (count != 4)
      return 1;
   expect_key(defs[0].key, "BLUS30443\x1ftReduce Audio Latency", 31);
   TEST_CHECK(strcmp(defs[0].desc, "Reduce Audio Latency") == 0);
   TEST_CHECK(strcmp(defs[1].desc, "Alpha") == 0);
   expect_key(defs[1].key, "BLUS30443\x1fpAlpha", 16);
   TEST_CHECK(strcmp(defs[1].info, "One patch of the group g at most is on. Applies at the next start of the game.") == 0);
   TEST_CHECK(strcmp(defs[2].desc, "Alpha: Mode") == 0);
   TEST_CHECK(strncmp(defs[2].key, defs[1].key, strlen(defs[1].key)) == 0 && defs[2].key[strlen(defs[1].key)] == '_');
   TEST_CHECK(strcmp(defs[2].values[0].value, "Low") == 0 && strcmp(defs[2].values[1].value, "High") == 0
         && !defs[2].values[2].value);
   TEST_CHECK(strcmp(defs[2].default_value, "High") == 0);
   TEST_CHECK(strcmp(defs[3].desc, "Zeta") == 0);
   TEST_CHECK(strcmp(defs[3].info, "First para line two (me) One patch of the group g at most is on. Applies at the next start of the game.") == 0);
   TEST_CHECK(strcmp(defs[3].default_value, "disabled") == 0);

   /* Both of the group turned on: the one turned on first keeps it */
   set(defs[1].key, "enabled");
   set(defs[3].key, "enabled");
   s_sets = s_shown = s_hidden = 0;
   patch_options_update_display(env, get, NULL);
   TEST_CHECK(s_sets == 1 && strcmp(get(NULL, defs[3].key), "disabled") == 0);
   TEST_CHECK(strcmp(get(NULL, defs[1].key), "enabled") == 0);
   TEST_CHECK(s_shown == 1 && s_hidden == 0);
   /* Then the other: the first goes off */
   set(defs[3].key, "enabled");
   s_sets = s_shown = s_hidden = 0;
   patch_options_update_display(env, get, NULL);
   TEST_CHECK(s_sets == 1 && strcmp(get(NULL, defs[1].key), "disabled") == 0);
   TEST_CHECK(s_shown == 0 && s_hidden == 1);

   /* patch_config.yml: Alpha with Low, for both executables; the text
    * patch on */
   set(defs[0].key, "enabled");
   set(defs[1].key, "enabled");
   set(defs[3].key, "disabled");
   set(defs[2].key, "Low");
   choices = patch_options_choices(get, NULL, &count);
   TEST_CHECK(game_text_patch_count() == 2);
   TEST_CHECK(count == 1);
   if (count == 1)
   {
      ryaml_writer_t *w = ryaml_writer_new(NULL, NULL);
      size_t len = 0;
      const char *out;
      TEST_CHECK(choices[0].description_len == 5 && memcmp(choices[0].description, "Alpha", 5) == 0);
      TEST_CHECK(strcmp(choices[0].serial_key, "BLUS30443") == 0);
      TEST_CHECK(choices[0].num_picks == 1 && choices[0].picks[0].value == 1);
      TEST_CHECK(patch_db_write_config(&db, choices, count, w));
      out = ryaml_writer_memory(w, &len);
      TEST_CHECK(out && strstr(out, "PPU-a") && strstr(out, "PPU-b") && strstr(out, "Mode: 1"));
      ryaml_writer_free(w);
   }

   /* Both of the group on in the .opt file: one written, a warning */
   set(defs[3].key, "enabled");
   s_warnings = 0;
   choices = patch_options_choices(get, NULL, &count);
   TEST_CHECK(count == 1 && s_warnings == 1);

   /* A game named for an "All titles" patch */
   patch_options_collect(&db, "BCES01097");
   TEST_CHECK(game_text_patch_count() == 0);
   defs = patch_options_definitions(&count);
   TEST_CHECK(count == 1);
   if (count == 1)
   {
      TEST_CHECK(strstr(defs[0].info, "For several games") != NULL);
      set(defs[0].key, "enabled");
      choices = patch_options_choices(get, NULL, &count);
      TEST_CHECK(count == 1 && strcmp(choices[0].serial_key, "All") == 0);
   }

   /* The frame rate the patches that are on ask for: a label's number, or
    * the value where the label has none; only of values named for it */
   patch_options_collect(&db, "BLES00009");
   defs = patch_options_definitions(&count);
   TEST_CHECK(count == 5);
   if (count == 5)
   {
      size_t speed = 5, rate = 5, cap = 5, fps = 5, d;
      for (d = 0; d < 5; d++)
      {
         if (!strcmp(defs[d].desc, "Speed")) speed = d;
         else if (!strcmp(defs[d].desc, "Speed: Frame Rate")) rate = d;
         else if (!strcmp(defs[d].desc, "Cap")) cap = d;
         else if (!strcmp(defs[d].desc, "Cap: Target FPS")) fps = d;
      }
      TEST_CHECK(speed < 5 && rate < 5 && cap < 5 && fps < 5);
      if (speed < 5 && rate < 5 && cap < 5 && fps < 5)
      {
         set(defs[speed].key, "disabled");
         set(defs[cap].key, "disabled");
         set(defs[rate].key, "120 FPS (Vblank Rate 120)");
         set(defs[fps].key, "Full");
         TEST_CHECK(patch_options_frame_rate(get, NULL) == 0);
         set(defs[speed].key, "enabled");
         TEST_CHECK(patch_options_frame_rate(get, NULL) == 120);
         set(defs[rate].key, "60 FPS");
         TEST_CHECK(patch_options_frame_rate(get, NULL) == 60);
         set(defs[cap].key, "enabled");
         TEST_CHECK(patch_options_frame_rate(get, NULL) == 90);
         set(defs[rate].key, "120 FPS (Vblank Rate 120)");
         TEST_CHECK(patch_options_frame_rate(get, NULL) == 120);
         set(defs[speed].key, "disabled");
         set(defs[fps].key, "Half");
         TEST_CHECK(patch_options_frame_rate(get, NULL) == 30);
      }
   }

   /* Games with none */
   patch_options_collect(&db, "XXXX00000");
   TEST_CHECK(!patch_options_any());
   patch_options_collect(&db, "");
   TEST_CHECK(!patch_options_any());
   defs = patch_options_definitions(&count);
   TEST_CHECK(count == 0);
   patch_options_collect(NULL, "BLUS30443");
   TEST_CHECK(patch_options_any());
   defs = patch_options_definitions(&count);
   TEST_CHECK(count == 1);

   patch_options_collect(NULL, NULL);
   patch_db_free(&db);
   if (test_failures)
   {
      fprintf(stderr, "test_patch_options: %d failures\n", test_failures);
      return 1;
   }
   printf("test_patch_options: ok\n");
   return 0;
}
