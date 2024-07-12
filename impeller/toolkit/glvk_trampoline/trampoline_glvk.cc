// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "impeller/toolkit/glvk_trampoline/trampoline_glvk.h"

#include "impeller/base/validation.h"

namespace impeller::glvk {

TrampolineGLVK::TrampolineGLVK(ProcTableGLVK::Resolver resolver)
    : gl_(resolver) {
  if (!gl_.IsValid()) {
    VALIDATION_LOG << "Could not setup trampoline proc table.";
    return;
  }

  // TODO(csg): Perform extensions check.

  is_valid_ = true;
}

TrampolineGLVK::~TrampolineGLVK() = default;

const ProcTableGLVK& TrampolineGLVK::GetProcTable() const {
  return gl_;
}

bool TrampolineGLVK::IsValid() const {
  return is_valid_;
}

}  // namespace impeller::glvk
