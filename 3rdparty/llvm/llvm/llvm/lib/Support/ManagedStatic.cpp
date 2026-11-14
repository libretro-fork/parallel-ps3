//===-- ManagedStatic.cpp - Static Global wrapper -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the ManagedStatic class and llvm_shutdown().
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/ManagedStatic.h"
#include "llvm/Config/config.h"
#include "llvm/Support/Threading.h"
#include <cassert>
#include <retro_atomic.h>
#include <thread>
using namespace llvm;

// libretro: no lock. The list of constructed statics is pushed to with a
// compare-and-swap; llvm_shutdown, which pops it, runs with no other thread
// in LLVM as it always had to.
static retro_atomic_ptr_t StaticList;

void ManagedStaticBase::RegisterManagedStatic(void *(*Creator)(),
                                              void (*Deleter)(void*)) const {
  assert(Creator);

  // One thread makes the object. A creator has effects beyond the object
  // (an option registers itself), so it is not run twice to throw one away.
  if (retro_atomic_cas_int(&Claimed, 0, 1)) {
    void *Tmp = Creator();
    DeleterFn = Deleter;

    // Add to list of managed statics.
    for (;;) {
      void *Head = retro_atomic_load_acquire_ptr(&StaticList);
      Next = static_cast<const ManagedStaticBase *>(Head);
      if (retro_atomic_cas_ptr(&StaticList, Head,
                               const_cast<ManagedStaticBase *>(this)))
        break;
    }

    Ptr.store(Tmp, std::memory_order_release);
    return;
  }

  // Another thread is making it: there is nothing to go on with until it is
  // made, as there was not under the lock
  while (!Ptr.load(std::memory_order_acquire))
    std::this_thread::yield();
}

void ManagedStaticBase::destroy() const {
  assert(DeleterFn && "ManagedStatic not initialized correctly!");
  assert(retro_atomic_load_acquire_ptr(&StaticList) == this &&
         "Not destroyed in reverse order of construction?");
  // Unlink from list.
  retro_atomic_store_release_ptr(&StaticList,
                                 const_cast<ManagedStaticBase *>(Next));
  Next = nullptr;

  // Destroy memory.
  DeleterFn(Ptr);

  // Cleanup.
  Ptr = nullptr;
  DeleterFn = nullptr;
  retro_atomic_store_release_int(&Claimed, 0);
}

/// llvm_shutdown - Deallocate and destroy all ManagedStatic variables.
/// IMPORTANT: it's only safe to call llvm_shutdown() in single thread,
/// without any other threads executing LLVM APIs.
/// llvm_shutdown() should be the last use of LLVM APIs.
void llvm::llvm_shutdown() {
  while (void *Head = retro_atomic_load_acquire_ptr(&StaticList))
    static_cast<const ManagedStaticBase *>(Head)->destroy();
}
