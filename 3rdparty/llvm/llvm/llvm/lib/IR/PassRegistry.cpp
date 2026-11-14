//===- PassRegistry.cpp - Pass Registration Implementation ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the PassRegistry, with which passes are registered on
// initialization, and supports the PassManager in dependency resolution.
//
//===----------------------------------------------------------------------===//

#include "llvm/PassRegistry.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Pass.h"
#include "llvm/PassInfo.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <memory>

using namespace llvm;

PassRegistry *PassRegistry::getPassRegistry() {
  static PassRegistry PassRegistryObj;
  return &PassRegistryObj;
}

//===----------------------------------------------------------------------===//
// Accessors
//

static const PassInfo *entryOf(void *P) {
  return reinterpret_cast<const PassInfo *>(reinterpret_cast<uintptr_t>(P) &
                                            ~uintptr_t(1));
}

static unsigned slotOf(const void *TI) {
  uintptr_t V = reinterpret_cast<uintptr_t>(TI);
  V ^= V >> 15;
  V *= 0x9E3779B97F4A7C15ull;
  return static_cast<unsigned>(V >> 32);
}

static unsigned slotOf(StringRef Arg) {
  // FNV-1a
  uint32_t H = 2166136261u;
  for (unsigned char C : Arg)
    H = (H ^ C) * 16777619u;
  return H;
}

PassRegistry::PassRegistry() {
  for (auto &E : ByID)
    retro_atomic_store_relaxed_ptr(&E, nullptr);
  for (auto &E : ByArg)
    retro_atomic_store_relaxed_ptr(&E, nullptr);
  for (auto &L : Listeners)
    retro_atomic_store_relaxed_ptr(&L, nullptr);
}

PassRegistry::~PassRegistry() {
  for (auto &E : ByID) {
    void *P = retro_atomic_load_relaxed_ptr(&E);
    if (reinterpret_cast<uintptr_t>(P) & 1)
      delete entryOf(P);
  }
}

const PassInfo *PassRegistry::getPassInfo(const void *TI) const {
  for (unsigned I = slotOf(TI), N = 0; N != TableSize; ++I, ++N) {
    void *P = retro_atomic_load_acquire_ptr(&ByID[I & (TableSize - 1)]);
    if (!P)
      return nullptr;
    if (entryOf(P)->getTypeInfo() == TI)
      return entryOf(P);
  }
  return nullptr;
}

const PassInfo *PassRegistry::getPassInfo(StringRef Arg) const {
  for (unsigned I = slotOf(Arg), N = 0; N != TableSize; ++I, ++N) {
    void *P = retro_atomic_load_acquire_ptr(&ByArg[I & (TableSize - 1)]);
    if (!P)
      return nullptr;
    if (entryOf(P)->getPassArgument() == Arg)
      return entryOf(P);
  }
  return nullptr;
}

void PassRegistry::registerPass(const PassInfo &PI, bool ShouldFree) {
  void *Entry = reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(&PI) |
                                         uintptr_t(ShouldFree));
  const void *TI = PI.getTypeInfo();
  bool Added = false;

  for (unsigned I = slotOf(TI), N = 0; N != TableSize; ++I, ++N) {
    retro_atomic_ptr_t *Slot = &ByID[I & (TableSize - 1)];
    void *P = retro_atomic_load_acquire_ptr(Slot);
    if (!P) {
      if (retro_atomic_cas_ptr(Slot, nullptr, Entry)) {
        Added = true;
        break;
      }
      // Another pass took the slot: look at it
      P = retro_atomic_load_acquire_ptr(Slot);
    }
    assert(entryOf(P)->getTypeInfo() != TI &&
           "Pass registered multiple times!");
    if (entryOf(P)->getTypeInfo() == TI)
      return;
  }

  if (!Added)
    report_fatal_error("PassRegistry: more passes than the table holds");

  StringRef Arg = PI.getPassArgument();
  for (unsigned I = slotOf(Arg), N = 0; N != TableSize; ++I, ++N) {
    retro_atomic_ptr_t *Slot = &ByArg[I & (TableSize - 1)];
    void *P = retro_atomic_load_acquire_ptr(Slot);
    if (!P) {
      if (retro_atomic_cas_ptr(Slot, nullptr, const_cast<PassInfo *>(&PI)))
        break;
      P = retro_atomic_load_acquire_ptr(Slot);
    }
    if (entryOf(P)->getPassArgument() == Arg)
      break;
  }

  // Notify any listeners.
  for (auto &L : Listeners)
    if (void *P = retro_atomic_load_acquire_ptr(&L))
      static_cast<PassRegistrationListener *>(P)->passRegistered(&PI);
}

void PassRegistry::enumerateWith(PassRegistrationListener *L) {
  for (auto &E : ByID)
    if (void *P = retro_atomic_load_acquire_ptr(&E))
      L->passEnumerate(entryOf(P));
}

void PassRegistry::addRegistrationListener(PassRegistrationListener *L) {
  for (auto &Slot : Listeners)
    if (!retro_atomic_load_acquire_ptr(&Slot) &&
        retro_atomic_cas_ptr(&Slot, nullptr, L))
      return;
  report_fatal_error("PassRegistry: more listeners than it holds");
}

void PassRegistry::removeRegistrationListener(PassRegistrationListener *L) {
  for (auto &Slot : Listeners)
    if (retro_atomic_load_acquire_ptr(&Slot) == L &&
        retro_atomic_cas_ptr(&Slot, L, nullptr))
      return;
}
