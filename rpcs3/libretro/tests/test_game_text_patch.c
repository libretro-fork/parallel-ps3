/* Game text patches (Emu/Cell/lv2/game_text_patch.c): Demon's Souls'
 * sound setup, the text built into its executable that the game parses
 * (it hands it to a memory stream with its length, 0x450), as Reduce Audio
 * Latency changes it at load. The engine reads PS3MixerBlockSize and
 * PS3MixerUpdateFrewuency (sic), which the text does not set; it does not
 * read PS3MixerPriority or PS3MultiStreamSpuPriority, whose lines become
 * the two keys at the same length. C89. */

#include "test_common.h"
#include "../../Emu/Cell/lv2/game_text_patch.h"

/* The text as BLUS30443's EBOOT.BIN has it: LF, tabs, no comments */
static const char s_text[] =
   "CueMaxNum\t\t\t= 400\n"
   "BankSetMaxNum\t\t= 30\n"
   "MixerMaxNum\t\t= 10\n"
   "EmitterMaxNum\t\t= 400\n"
   "DebugLevel\t\t\t= 1\n"
   "SoundOfSpeed\t\t= 340.29\n"
   "MessageBuffSize\t= 16384\n"
   "MeterFactor\t\t= 1\n"
   "\n"
   "PS3SoundHomePath\t\t\t= sound:\n"
   "PS3GlobalSettingPath\t\t= ${PS3SoundHomePath}/magicorchestra2.mgs\n"
   "PS3RPCSettingPath\t\t\t= ${PS3SoundHomePath}/rpcsetting.rpc\n"
   "PS3MixerDataPath\t\t\t= ${PS3SoundHomePath}/demonsmixer.momd\n"
   "PS3MultiStreamMaxNum\t\t= 4\n"
   "PS3SubMaxNum\t\t\t\t= 3\n"
   "PS3StreamBuffSize\t\t\t= 40960\n"
   "PS3StreamLoopBuffNum\t\t= 3\n"
   "PS3StreamLoopBuffSize\t\t= 204800\n"
   "PS3Atrack3Num\t\t\t\t= 3\n"
   "PS3Atrack3WorkBuffSize\t\t= 389120\n"
   "PS3SpursPriority_0\t\t\t= 1\n"
   "PS3SpursPriority_1\t\t\t= 1\n"
   "PS3SpursPriority_2\t\t\t= 1\n"
   "PS3SpursPriority_3\t\t\t= 1\n"
   "PS3SpursPriority_4\t\t\t= 0\n"
   "PS3SpursPriority_5\t\t\t= 0\n"
   "PS3SpursPriority_6\t\t\t= 0\n"
   "PS3SpursPriority_7\t\t\t= 0\n"
   "PS3MultiStreamSpuNum \t\t= 1\n"
   "PS3MultiStreamSpuPriority\t= 250\n"
   "PS3MakeMOTranslator\t\t= MOHDDTranslator\n"
   "PS3DopplerParamIndex\t\t= 4\n"
   "PS3AttackParamIndex\t\t= 1\n"
   "PS3ReleaseParamIndex\t\t= 2\n"
   "PS3NumInstancesIndex\t\t= 0\n"
   "PS3MixerPriority\t\t\t= 400\n"
   "PS3MixerCHStreap1Num\t\t= 2\n"
   "PS3MixerCHStreap2Num\t\t= 3\n"
   "PS3MixerCHStreap6Num\t\t= 2\n"
   "PS3MixerCHStreap8Num\t\t= 2\n"
   "PS3SleepCount\t\t\t\t= 5\n"
;

/* What is around it in the executable: other strings, zero-terminated */
static const char s_before[] = "MoBankResCap\0\0\0";
static const char s_after[]  = "\0MOSB%d";

#define SEG_LEN (sizeof(s_before) - 1 + sizeof(s_text) - 1 + sizeof(s_after) - 1)

static void make_segment(char *seg)
{
   memcpy(seg, s_before, sizeof(s_before) - 1);
   memcpy(seg + sizeof(s_before) - 1, s_text, sizeof(s_text) - 1);
   memcpy(seg + sizeof(s_before) - 1 + sizeof(s_text) - 1, s_after, sizeof(s_after) - 1);
}

/* The line of @text that starts with @start, without its LF */
static size_t line_of(const char *text, size_t len, const char *start, char *out, size_t cap)
{
   size_t i, n = 0, s = strlen(start);
   for (i = 0; i + s <= len; i++)
   {
      if ((i == 0 || text[i - 1] == '\n') && !memcmp(text + i, start, s))
      {
         while (i + n < len && text[i + n] != '\n' && text[i + n] != '\0' && n + 1 < cap)
         {
            out[n] = text[i + n];
            n++;
         }
         break;
      }
   }
   out[n] = '\0';
   return n;
}

int main(void)
{
   static char seg[SEG_LEN];
   static char want[SEG_LEN];
   char line[128], old_line[128];
   const size_t text_at = sizeof(s_before) - 1;
   size_t n, i;

   TEST_CHECK(sizeof(s_text) == 0x450); /* what the game passes */

   /* Both edits through the Patch Manager's path */
   game_text_patch_clear();
   TEST_CHECK(game_text_patch_add("PS3MixerCHStreap8Num", "PS3MixerPriority", "PS3MixerBlockSize", 8));
   TEST_CHECK(game_text_patch_add("PS3MixerCHStreap8Num", "PS3MultiStreamSpuPriority", "PS3MixerUpdateFrewuency", 2));
   TEST_CHECK(game_text_patch_count() == 2);
   make_segment(seg);
   TEST_CHECK(game_text_patch_apply(seg, SEG_LEN) == 2);

   /* The two lines at their old lengths, the key and "= value" with spaces
    * between */
   n = line_of(seg + text_at, sizeof(s_text) - 1, "PS3MixerBlockSize", line, sizeof(line));
   TEST_CHECK(n == line_of(s_text, sizeof(s_text) - 1, "PS3MixerPriority", old_line, sizeof(old_line)));
   TEST_CHECK(!strncmp(line, "PS3MixerBlockSize ", 18) && !strcmp(line + n - 3, "= 8"));
   TEST_CHECK(strspn(line + 17, " ") == n - 17 - 3);
   n = line_of(seg + text_at, sizeof(s_text) - 1, "PS3MixerUpdateFrewuency", line, sizeof(line));
   TEST_CHECK(n == line_of(s_text, sizeof(s_text) - 1, "PS3MultiStreamSpuPriority", old_line, sizeof(old_line)));
   TEST_CHECK(!strncmp(line, "PS3MixerUpdateFrewuency ", 24) && !strcmp(line + n - 3, "= 2"));

   /* Everything else as it was: the strings around it, every other byte of
    * the text, its zero */
   make_segment(want);
   for (i = 0; i < SEG_LEN; i++)
   {
      if (seg[i] != want[i])
      {
         const char *l = seg + i;
         while (l > seg && l[-1] != '\n')
            l--;
         if (strncmp(l, "PS3MixerBlockSize", 17) && strncmp(l, "PS3MixerUpdateFrewuency", 23))
         {
            TEST_CHECK(!"a byte changed outside the two lines");
            break;
         }
      }
   }
   TEST_CHECK(!memcmp(seg, s_before, sizeof(s_before) - 1));
   TEST_CHECK(!memcmp(seg + SEG_LEN - (sizeof(s_after) - 1), s_after, sizeof(s_after) - 1));
   TEST_CHECK(strstr(seg + text_at, "PS3MixerCHStreap8Num\t\t= 2\nPS3SleepCount\t\t\t\t= 5\n") != NULL);

   /* Applied again: nothing left to change */
   TEST_CHECK(game_text_patch_apply(seg, SEG_LEN) == 0);

   /* No anchor, nothing changed; a victim outside the anchor's text is not
    * touched */
   {
      static const char src[] = "PS3MixerPriority\t\t\t= 400\n\0no anchor here";
      static char other[64];
      memcpy(other, src, sizeof(src));
      TEST_CHECK(game_text_patch_apply(other, sizeof(other)) == 0);
      TEST_CHECK(!strcmp(other, "PS3MixerPriority\t\t\t= 400\n"));
   }

   /* Only the text that holds the anchor: the same key in another string
    * before it is left alone */
   {
      static const char first[] = "PS3MixerPriority\t\t\t= 999\n";
      static char two[sizeof(first) + sizeof(s_text)];
      memcpy(two, first, sizeof(first));
      memcpy(two + sizeof(first), s_text, sizeof(s_text));
      game_text_patch_clear();
      TEST_CHECK(game_text_patch_add("PS3MixerCHStreap8Num", "PS3MixerPriority", "PS3MixerBlockSize", 8));
      TEST_CHECK(game_text_patch_apply(two, sizeof(two)) == 1);
      TEST_CHECK(!strcmp(two, first));
      TEST_CHECK(strstr(two + sizeof(first), "\nPS3MixerBlockSize    = 8\n") != NULL);
   }

   /* A line too short, a key that is only the start of another, and a key
    * after other text on its line are left alone */
   {
      char shortline[] = "PS3MixerPriority=40\n";
      char longer[] = "PS3MixerPriorityMax\t\t\t= 400\n";
      char inside[] = "# PS3MixerPriority\t\t\t= 400\n";
      TEST_CHECK(game_text_patch_replace_line(shortline, strlen(shortline), "PS3MixerPriority", "PS3MixerBlockSize", 8) == 0);
      TEST_CHECK(!strcmp(shortline, "PS3MixerPriority=40\n"));
      TEST_CHECK(game_text_patch_replace_line(longer, strlen(longer), "PS3MixerPriority", "PS3MixerBlockSize", 8) == 0);
      TEST_CHECK(game_text_patch_replace_line(inside, strlen(inside), "PS3MixerPriority", "PS3MixerBlockSize", 8) == 0);
   }

   /* A CR LF text keeps its CR */
   {
      char crlf[] = "PS3MixerPriority\t\t\t= 400\r\nX = 1\r\n";
      TEST_CHECK(game_text_patch_replace_line(crlf, strlen(crlf), "PS3MixerPriority", "PS3MixerBlockSize", 8) == 1);
      TEST_CHECK(!strcmp(crlf, "PS3MixerBlockSize    = 8\r\nX = 1\r\n"));
   }

   /* A key rewritten as itself with another value (Dark Souls'
    * MainThreadUpdateTime): the line keeps its length, its neighbours
    * are untouched, and a longer key that starts the same is not taken
    * for it */
   {
      char same[] = "MainThreadUpdateTimeX\t= 1\nFmodStreamBuffSize\t\t\t= 196608\nMainThreadUpdateTime\t\t= 33333\nUseFmodLoader\t\t\t\t= FALSE\n";
      TEST_CHECK(game_text_patch_replace_line(same, strlen(same), "MainThreadUpdateTime", "MainThreadUpdateTime", 16666) == 1);
      TEST_CHECK(!strcmp(same, "MainThreadUpdateTimeX\t= 1\nFmodStreamBuffSize\t\t\t= 196608\nMainThreadUpdateTime  = 16666\nUseFmodLoader\t\t\t\t= FALSE\n"));
   }

   game_text_patch_clear();
   TEST_CHECK(game_text_patch_count() == 0);
   make_segment(seg);
   TEST_CHECK(game_text_patch_apply(seg, SEG_LEN) == 0);

   printf("test_game_text_patch: %s\n", test_failures ? "FAIL" : "ok");
   return test_failures ? 1 : 0;
}
