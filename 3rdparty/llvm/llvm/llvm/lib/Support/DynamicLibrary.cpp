//===-- DynamicLibrary.cpp - Runtime link/load libraries --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements the operating system DynamicLibrary concept.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/DynamicLibrary.h"
#include "llvm-c/Support.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Config/config.h"
#include "llvm/Support/ErrorHandling.h"
#include <retro_atomic.h>
#include <string>
#include <vector>

using namespace llvm;
using namespace llvm::sys;

// libretro: no lock. The handles are a fixed array of slots, each taken and
// given up with one compare-and-swap; the process handle is one word; the
// symbols given by hand are a list that is only ever added to at its head.
// Lookups walk them from any thread while others add.
class DynamicLibrary::HandleSet {
  enum { MaxHandles = 64 };
  retro_atomic_ptr_t Handles[MaxHandles];
  retro_atomic_ptr_t Process;

public:
  static void *DLOpen(const char *Filename, std::string *Err);
  static void DLClose(void *Handle);
  static void *DLSym(void *Handle, const char *Symbol);

  HandleSet() {
    for (auto &H : Handles)
      retro_atomic_store_relaxed_ptr(&H, nullptr);
    retro_atomic_store_relaxed_ptr(&Process, &Invalid);
  }
  ~HandleSet();

  void *process() { return retro_atomic_load_acquire_ptr(&Process); }
  void dropProcess() { retro_atomic_store_release_ptr(&Process, &Invalid); }

  // Every handle, in the order of their slots or the other way round
  template <typename F> void *eachHandle(bool Reverse, F &&Fn) {
    for (int N = 0; N != MaxHandles; ++N) {
      int I = Reverse ? MaxHandles - 1 - N : N;
      if (void *Handle = retro_atomic_load_acquire_ptr(&Handles[I]))
        if (void *Ptr = Fn(Handle))
          return Ptr;
    }
    return nullptr;
  }

  bool Contains(void *Handle) {
    return Handle == process() ||
           eachHandle(false, [&](void *H) { return H == Handle ? H : nullptr; });
  }

  bool AddLibrary(void *Handle, bool IsProcess = false, bool CanClose = true,
                  bool AllowDuplicates = false) {
#ifdef _WIN32
    assert((Handle == this ? IsProcess : !IsProcess) && "Bad Handle.");
#endif
    assert((!AllowDuplicates || !CanClose) &&
           "CanClose must be false if AllowDuplicates is true.");

    if (LLVM_LIKELY(!IsProcess)) {
      if (!AllowDuplicates && Contains(Handle)) {
        if (CanClose)
          DLClose(Handle);
        return false;
      }
      for (auto &Slot : Handles)
        if (!retro_atomic_load_acquire_ptr(&Slot) &&
            retro_atomic_cas_ptr(&Slot, nullptr, Handle))
          return true;
      report_fatal_error("DynamicLibrary: more libraries than it holds");
    } else {
      void *Old = retro_atomic_exchange_ptr(&Process, Handle);
#ifndef _WIN32
      if (Old != &Invalid) {
        if (CanClose)
          DLClose(Old);
        if (Old == Handle)
          return false;
      }
#else
      (void)Old;
#endif
    }
    return true;
  }

  void CloseLibrary(void *Handle) {
    DLClose(Handle);
    for (auto &Slot : Handles)
      if (retro_atomic_load_acquire_ptr(&Slot) == Handle &&
          retro_atomic_cas_ptr(&Slot, Handle, nullptr))
        return;
  }

  void *LibLookup(const char *Symbol, DynamicLibrary::SearchOrdering Order) {
    return eachHandle(!(Order & SO_LoadOrder),
                      [&](void *Handle) { return DLSym(Handle, Symbol); });
  }

  void *Lookup(const char *Symbol, DynamicLibrary::SearchOrdering Order) {
    assert(!((Order & SO_LoadedFirst) && (Order & SO_LoadedLast)) &&
           "Invalid Ordering");

    void *Proc = process();

    if (Proc == &Invalid || (Order & SO_LoadedFirst)) {
      if (void *Ptr = LibLookup(Symbol, Order))
        return Ptr;
    }
    if (Proc != &Invalid) {
      // Use OS facilities to search the current binary and all loaded libs.
      if (void *Ptr = DLSym(Proc, Symbol))
        return Ptr;

      // Search any libs that might have been skipped because of RTLD_LOCAL.
      if (Order & SO_LoadedLast) {
        if (void *Ptr = LibLookup(Symbol, Order))
          return Ptr;
      }
    }
    return nullptr;
  }
};

namespace {

// A symbol given by hand (AddSymbol). The newest of a name is at the front.
struct ExplicitSymbol {
  std::string Name;
  void *Value;
  ExplicitSymbol *Next;
};

struct Globals {
  retro_atomic_ptr_t ExplicitSymbols;
  DynamicLibrary::HandleSet OpenedHandles;
  DynamicLibrary::HandleSet OpenedTemporaryHandles;

  Globals() { retro_atomic_store_relaxed_ptr(&ExplicitSymbols, nullptr); }
};

Globals &getGlobals() {
  static Globals G;
  return G;
}

} // namespace

#ifdef _WIN32

#include "Windows/DynamicLibrary.inc"

#else

#include "Unix/DynamicLibrary.inc"

#endif

char DynamicLibrary::Invalid;
DynamicLibrary::SearchOrdering DynamicLibrary::SearchOrder =
    DynamicLibrary::SO_Linker;

namespace llvm {
void *SearchForAddressOfSpecialSymbol(const char *SymbolName) {
  return DoSearch(SymbolName); // DynamicLibrary.inc
}
} // namespace llvm

void DynamicLibrary::AddSymbol(StringRef SymbolName, void *SymbolValue) {
  auto &G = getGlobals();
  auto *Added = new ExplicitSymbol{SymbolName.str(), SymbolValue, nullptr};

  for (;;) {
    void *Head = retro_atomic_load_acquire_ptr(&G.ExplicitSymbols);
    Added->Next = static_cast<ExplicitSymbol *>(Head);
    if (retro_atomic_cas_ptr(&G.ExplicitSymbols, Head, Added))
      break;
  }
}

DynamicLibrary DynamicLibrary::getPermanentLibrary(const char *FileName,
                                                   std::string *Err) {
  auto &G = getGlobals();
  void *Handle = HandleSet::DLOpen(FileName, Err);
  if (Handle != &Invalid)
    G.OpenedHandles.AddLibrary(Handle, /*IsProcess*/ FileName == nullptr);

  return DynamicLibrary(Handle);
}

DynamicLibrary DynamicLibrary::addPermanentLibrary(void *Handle,
                                                   std::string *Err) {
  auto &G = getGlobals();
  // If we've already loaded this library, tell the caller.
  if (!G.OpenedHandles.AddLibrary(Handle, /*IsProcess*/ false,
                                  /*CanClose*/ false))
    *Err = "Library already loaded";

  return DynamicLibrary(Handle);
}

DynamicLibrary DynamicLibrary::getLibrary(const char *FileName,
                                          std::string *Err) {
  assert(FileName && "Use getPermanentLibrary() for opening process handle");
  void *Handle = HandleSet::DLOpen(FileName, Err);
  if (Handle != &Invalid) {
    auto &G = getGlobals();
    G.OpenedTemporaryHandles.AddLibrary(Handle, /*IsProcess*/ false,
                                        /*CanClose*/ false,
                                        /*AllowDuplicates*/ true);
  }
  return DynamicLibrary(Handle);
}

void DynamicLibrary::closeLibrary(DynamicLibrary &Lib) {
  auto &G = getGlobals();
  if (Lib.isValid()) {
    G.OpenedTemporaryHandles.CloseLibrary(Lib.Data);
    Lib.Data = &Invalid;
  }
}

void *DynamicLibrary::getAddressOfSymbol(const char *SymbolName) {
  if (!isValid())
    return nullptr;
  return HandleSet::DLSym(Data, SymbolName);
}

void *DynamicLibrary::SearchForAddressOfSymbol(const char *SymbolName) {
  {
    auto &G = getGlobals();

    // First check symbols added via AddSymbol().
    for (auto *S = static_cast<ExplicitSymbol *>(
             retro_atomic_load_acquire_ptr(&G.ExplicitSymbols));
         S; S = S->Next)
      if (S->Name == SymbolName)
        return S->Value;

    // Now search the libraries.
    if (void *Ptr = G.OpenedHandles.Lookup(SymbolName, SearchOrder))
      return Ptr;
    if (void *Ptr = G.OpenedTemporaryHandles.Lookup(SymbolName, SearchOrder))
      return Ptr;
  }

  return llvm::SearchForAddressOfSpecialSymbol(SymbolName);
}

//===----------------------------------------------------------------------===//
// C API.
//===----------------------------------------------------------------------===//

LLVMBool LLVMLoadLibraryPermanently(const char *Filename) {
  return llvm::sys::DynamicLibrary::LoadLibraryPermanently(Filename);
}

void *LLVMSearchForAddressOfSymbol(const char *symbolName) {
  return llvm::sys::DynamicLibrary::SearchForAddressOfSymbol(symbolName);
}

void LLVMAddSymbol(const char *symbolName, void *symbolValue) {
  return llvm::sys::DynamicLibrary::AddSymbol(symbolName, symbolValue);
}
