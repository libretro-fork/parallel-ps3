/* vblank_unlock.txt: see vblank_list.h. C89. */

#include <string.h>

#include "vblank_list.h"

static int is_blank(char c)
{
   return c == ' ' || c == '\t' || c == '\r';
}

unsigned vblank_list_block(const char *text, size_t len, const char *serial)
{
   const char *p     = text;
   const char *end   = text + len;
   size_t serial_len = strlen(serial);
   unsigned block    = 0;

   if (!serial_len)
      return 0;

   while (p < end)
   {
      const char *eol = (const char*)memchr(p, '\n', (size_t)(end - p));
      const char *e;
      const char *hash;

      if (!eol)
         eol = end;
      e = eol;
      if ((hash = (const char*)memchr(p, '#', (size_t)(e - p))))
         e = hash;
      while (p < e && is_blank(*p))
         p++;
      while (e > p && is_blank(e[-1]))
         e--;

      if (p < e)
      {
         size_t n = (size_t)(e - p);
         if (*p == '[')
            block = n >= 3 && !memcmp(p, "[30", 3) ? 30
                  : n >= 3 && !memcmp(p, "[60", 3) ? 60 : 0;
         else if (block && n == serial_len && !memcmp(p, serial, n))
            return block;
      }

      p = eol + 1;
   }

   return 0;
}
