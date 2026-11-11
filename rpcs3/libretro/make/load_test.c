/* `make check`: the core the Makefile built, opened as a frontend opens it
 * but with every symbol resolved at once (RTLD_NOW), so a function the link
 * left undefined fails here and not in a game; then asked what it is. */
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

struct retro_system_info
{
   const char *library_name;
   const char *library_version;
   const char *valid_extensions;
   int         need_fullpath;
   int         block_extract;
};

int main(int argc, char **argv)
{
   void *core;
   void (*get_info)(struct retro_system_info *);
   unsigned (*api_version)(void);
   struct retro_system_info info;
   int ok;

   if (argc < 2)
      return 2;
   if (!(core = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL)))
   {
      fprintf(stderr, "load_test: %s\n", dlerror());
      return 1;
   }
   get_info    = (void (*)(struct retro_system_info *))dlsym(core, "retro_get_system_info");
   api_version = (unsigned (*)(void))dlsym(core, "retro_api_version");
   if (!get_info || !api_version)
   {
      fprintf(stderr, "load_test: the core does not export the libretro API\n");
      return 1;
   }
   memset(&info, 0, sizeof(info));
   get_info(&info);
   printf("load_test: %s %s, API %u\n", info.library_name ? info.library_name : "?",
         info.library_version ? info.library_version : "?", api_version());
   /* the strings are the core's: looked at before it is closed */
   ok = info.library_name && !strcmp(info.library_name, "ParaLLEl PS3") && api_version() == 1;
   dlclose(core);
   return ok ? 0 : 1;
}
