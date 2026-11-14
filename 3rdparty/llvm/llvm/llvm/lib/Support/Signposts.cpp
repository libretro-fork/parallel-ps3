//===-- Signposts.cpp - Interval debug annotations ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/Signposts.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Config/config.h"

// libretro: the signposts of Xcode's tools, the one implementation there
// was (with its lock), are not here: they were configured off on every
// system this library is built for.

using namespace llvm;

/// Definition necessary for use of std::unique_ptr in SignpostEmitter::Impl.
class llvm::SignpostEmitterImpl {};

#define HAVE_ANY_SIGNPOST_IMPL 0

SignpostEmitter::SignpostEmitter() {
#if HAVE_ANY_SIGNPOST_IMPL
  Impl = std::make_unique<SignpostEmitterImpl>();
#endif // if !HAVE_ANY_SIGNPOST_IMPL
}

SignpostEmitter::~SignpostEmitter() = default;

bool SignpostEmitter::isEnabled() const {
#if HAVE_ANY_SIGNPOST_IMPL
  return Impl->isEnabled();
#else
  return false;
#endif // if !HAVE_ANY_SIGNPOST_IMPL
}

void SignpostEmitter::startInterval(const void *O, StringRef Name) {
#if HAVE_ANY_SIGNPOST_IMPL
  if (Impl == nullptr)
    return;
  return Impl->startInterval(O, Name);
#endif // if !HAVE_ANY_SIGNPOST_IMPL
}

void SignpostEmitter::endInterval(const void *O, StringRef Name) {
#if HAVE_ANY_SIGNPOST_IMPL
  if (Impl == nullptr)
    return;
  Impl->endInterval(O, Name);
#endif // if !HAVE_ANY_SIGNPOST_IMPL
}
