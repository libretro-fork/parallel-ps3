#include "jit_host_symbols.h"

#include <string.h>

#include <retro_atomic.h>

/* A slot is taken with a compare-and-swap (EMPTY to TAKEN), filled by the
 * thread that took it, and read by others only once it says READY. Slots
 * are never given back, so a lookup is a walk over the ones in use.
 *
 * Two threads noting a new name at once both take a slot for it: lookups
 * read every slot of the name, so two slots with one address are that
 * address and two with different ones are no answer. */
#define SLOT_EMPTY 0
#define SLOT_TAKEN 1
#define SLOT_READY 2

typedef struct
{
   retro_atomic_int_t state;
   unsigned len;
   uintptr_t addr;
   char name[JIT_HOST_SYMBOL_NAME + 1];
} host_symbol_t;

static host_symbol_t symbols[JIT_HOST_SYMBOLS];
static retro_atomic_int_t symbols_used;
static retro_atomic_int_t symbols_ambiguous;

/* 0: not there. 1: there, with this address. 2: there, with another. */
static int look(const char *name, size_t len, uintptr_t *addr)
{
   int used  = retro_atomic_load_acquire_int(&symbols_used);
   int found = 0;
   int i;

   for (i = 0; i < used; i++)
   {
      const host_symbol_t *s = &symbols[i];

      if (retro_atomic_load_acquire_int((retro_atomic_int_t*)&s->state) != SLOT_READY)
         continue;
      if (s->len != len || memcmp(s->name, name, len))
         continue;

      if (!found)
      {
         *addr = s->addr;
         found = 1;
      }
      else if (s->addr != *addr)
         return 2;
   }

   return found;
}

void jit_host_symbol_note(const char *name, size_t len, uintptr_t addr)
{
   uintptr_t have = 0;
   int i;

   if (!len || len > JIT_HOST_SYMBOL_NAME || !addr)
      return;

   switch (look(name, len, &have))
   {
   case 1:
      if (have == addr)
         return;
      /* Another function under this name: a second slot says so */
      retro_atomic_fetch_add_int(&symbols_ambiguous, 1);
      break;
   case 2:
      return;
   default:
      break;
   }

   for (i = 0; i < JIT_HOST_SYMBOLS; i++)
   {
      host_symbol_t *s = &symbols[i];

      if (retro_atomic_load_acquire_int(&s->state) != SLOT_EMPTY)
         continue;
      if (!retro_atomic_cas_int(&s->state, SLOT_EMPTY, SLOT_TAKEN))
         continue;

      memcpy(s->name, name, len);
      s->name[len] = '\0';
      s->len       = (unsigned)len;
      s->addr      = addr;

      /* Lookups walk up to the highest slot in use */
      for (;;)
      {
         int used = retro_atomic_load_acquire_int(&symbols_used);

         if (used > i || retro_atomic_cas_int(&symbols_used, used, i + 1))
            break;
      }

      retro_atomic_store_release_int(&s->state, SLOT_READY);
      return;
   }

   /* Full: the name is not answered for, and its objects are loaded the
    * long way */
}

uintptr_t jit_host_symbol_find(const char *name, size_t len)
{
   uintptr_t addr = 0;

   if (!len || len > JIT_HOST_SYMBOL_NAME)
      return 0;

   return look(name, len, &addr) == 1 ? addr : 0;
}

unsigned jit_host_symbols_ambiguous(void)
{
   return (unsigned)retro_atomic_load_acquire_int(&symbols_ambiguous);
}

void jit_host_symbols_reset(void)
{
   int i;

   for (i = 0; i < JIT_HOST_SYMBOLS; i++)
      retro_atomic_store_release_int(&symbols[i].state, SLOT_EMPTY);

   retro_atomic_store_release_int(&symbols_used, 0);
   retro_atomic_store_release_int(&symbols_ambiguous, 0);
}
