#pragma once

/* The host functions compiled guest code calls, by the name the compiled
 * object knows them under: where each is in this process.
 *
 * A recompiler names a host function when it builds a call to it, and gives
 * the JIT its address then. An object loaded from the cache without its
 * module being built again still has those names to link, and nothing that
 * says where they are: this table does, once a name has been seen in this
 * process. A name seen with two addresses is not answered for (two functions
 * under one name: the object could mean either).
 *
 * Lock-free: notes and lookups from any thread. */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define JIT_HOST_SYMBOLS      128
#define JIT_HOST_SYMBOL_NAME  63

/* A name and its address, as a call to it is built */
void jit_host_symbol_note(const char *name, size_t len, uintptr_t addr);

/* Its address; 0 when the name was not seen yet, or with two addresses */
uintptr_t jit_host_symbol_find(const char *name, size_t len);

/* How many names were seen with two addresses */
unsigned jit_host_symbols_ambiguous(void);

/* Forgets every name. Not while another thread notes or looks up. */
void jit_host_symbols_reset(void);

#ifdef __cplusplus
}
#endif
