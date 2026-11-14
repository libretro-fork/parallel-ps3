/* The table of host functions compiled code calls, by name
 * (Emu/CPU/jit_host_symbols.c): what an object loaded from the cache
 * without its module is linked by.
 *
 * - A name noted is found, with its address; one not noted is not.
 * - A name noted again with the same address is one name; with another
 *   address it is answered for no more, and counted.
 * - Names too long, empty or with no address are not kept; a full table
 *   keeps what it has.
 * - Threads noting and looking up at once: a lookup gives the name's one
 *   address or nothing, never another name's. */

#include "../../Emu/CPU/jit_host_symbols.h"
#include "test_common.h"

#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <retro_atomic.h>

static uintptr_t addr_of(unsigned n) { return (uintptr_t)0x100000u + n * 16u; }

static void name_of(unsigned n, char *out)
{
   sprintf(out, "host_function_%03u", n);
}

static void alone(void)
{
   char name[80];
   unsigned i;

   jit_host_symbols_reset();
   TEST_CHECK(jit_host_symbol_find("spu_read_channel", 16) == 0);

   jit_host_symbol_note("spu_read_channel", 16, 0x1000);
   jit_host_symbol_note("spu_write_channel", 17, 0x2000);
   TEST_CHECK(jit_host_symbol_find("spu_read_channel", 16) == 0x1000);
   TEST_CHECK(jit_host_symbol_find("spu_write_channel", 17) == 0x2000);
   /* Not by a part of a name */
   TEST_CHECK(jit_host_symbol_find("spu_read_channel", 15) == 0);
   TEST_CHECK(jit_host_symbol_find("spu_read_channel_count", 22) == 0);

   /* The same again is the same */
   jit_host_symbol_note("spu_read_channel", 16, 0x1000);
   TEST_CHECK(jit_host_symbol_find("spu_read_channel", 16) == 0x1000);
   TEST_CHECK(jit_host_symbols_ambiguous() == 0);

   /* Another function under the name: no answer from then on */
   jit_host_symbol_note("spu_read_channel", 16, 0x3000);
   TEST_CHECK(jit_host_symbol_find("spu_read_channel", 16) == 0);
   TEST_CHECK(jit_host_symbols_ambiguous() == 1);
   jit_host_symbol_note("spu_read_channel", 16, 0x1000);
   jit_host_symbol_note("spu_read_channel", 16, 0x4000);
   TEST_CHECK(jit_host_symbol_find("spu_read_channel", 16) == 0);
   TEST_CHECK(jit_host_symbols_ambiguous() == 1);
   TEST_CHECK(jit_host_symbol_find("spu_write_channel", 17) == 0x2000);

   /* What is not kept */
   memset(name, 'x', sizeof(name));
   jit_host_symbol_note(name, JIT_HOST_SYMBOL_NAME + 1, 0x5000);
   TEST_CHECK(jit_host_symbol_find(name, JIT_HOST_SYMBOL_NAME + 1) == 0);
   jit_host_symbol_note(name, JIT_HOST_SYMBOL_NAME, 0x5000);
   TEST_CHECK(jit_host_symbol_find(name, JIT_HOST_SYMBOL_NAME) == 0x5000);
   jit_host_symbol_note("", 0, 0x6000);
   jit_host_symbol_note("nowhere", 7, 0);
   TEST_CHECK(jit_host_symbol_find("", 0) == 0);
   TEST_CHECK(jit_host_symbol_find("nowhere", 7) == 0);

   /* Full: what is in stays, what does not fit is not answered for */
   jit_host_symbols_reset();
   TEST_CHECK(jit_host_symbol_find("spu_write_channel", 17) == 0);
   for (i = 0; i < JIT_HOST_SYMBOLS + 40; i++)
   {
      name_of(i, name);
      jit_host_symbol_note(name, strlen(name), addr_of(i));
   }
   for (i = 0; i < JIT_HOST_SYMBOLS + 40; i++)
   {
      name_of(i, name);
      TEST_CHECK(jit_host_symbol_find(name, strlen(name)) == (i < JIT_HOST_SYMBOLS ? addr_of(i) : 0));
   }
   TEST_CHECK(jit_host_symbols_ambiguous() == 0);
}

#define NAMES   60
#define ROUNDS  20000

static retro_atomic_int_t go, wrong, found_some;

static void *noter(void *arg)
{
   unsigned x = (unsigned)(size_t)arg * 2654435761u + 1;
   char name[80];
   int i;

   while (!retro_atomic_load_acquire_int(&go))
      sched_yield();

   for (i = 0; i < ROUNDS; i++)
   {
      unsigned n;
      x = x * 1664525u + 1013904223u;
      n = (x >> 16) % NAMES;
      name_of(n, name);
      jit_host_symbol_note(name, strlen(name), addr_of(n));
   }

   /* One name every thread gives its own address */
   jit_host_symbol_note("two_functions", 13, addr_of(1000 + (unsigned)(size_t)arg));
   return NULL;
}

static void *looker(void *arg)
{
   unsigned x = (unsigned)(size_t)arg * 40503u + 7;
   char name[80];
   int i;

   while (!retro_atomic_load_acquire_int(&go))
      sched_yield();

   for (i = 0; i < ROUNDS; i++)
   {
      unsigned n;
      uintptr_t a;
      x = x * 1664525u + 1013904223u;
      n = (x >> 16) % NAMES;
      name_of(n, name);
      a = jit_host_symbol_find(name, strlen(name));

      if (a == addr_of(n))
         retro_atomic_store_release_int(&found_some, 1);
      else if (a)
         retro_atomic_fetch_add_int(&wrong, 1);
   }

   return NULL;
}

static void together(void)
{
   pthread_t t[6];
   char name[80];
   size_t i;
   unsigned n;

   jit_host_symbols_reset();
   for (i = 0; i < 6; i++)
      pthread_create(&t[i], NULL, i < 3 ? noter : looker, (void*)i);
   retro_atomic_store_release_int(&go, 1);
   for (i = 0; i < 6; i++)
      pthread_join(t[i], NULL);

   TEST_CHECK(retro_atomic_load_acquire_int(&wrong) == 0);

   /* Every name the three went through is there, once as far as a lookup
    * can tell, and the one with three addresses is not answered for */
   for (n = 0; n < NAMES; n++)
   {
      name_of(n, name);
      TEST_CHECK(jit_host_symbol_find(name, strlen(name)) == addr_of(n));
   }
   TEST_CHECK(jit_host_symbol_find("two_functions", 13) == 0);
   TEST_CHECK(jit_host_symbols_ambiguous() <= 2);
   (void)found_some;
}

int main(void)
{
   alarm(300);
   alone();
   together();
   printf("test_jit_host_symbols: %s\n", test_failures ? "FAIL" : "ok");
   return test_failures ? 1 : 0;
}
