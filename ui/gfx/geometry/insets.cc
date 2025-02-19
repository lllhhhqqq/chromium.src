// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/gfx/geometry/insets.h"

#include "base/strings/stringprintf.h"

namespace gfx {

Insets::Insets() : Insets(0) {}

Insets::Insets(int all) : Insets(all, all, all, all) {}

Insets::Insets(int vertical, int horizontal)
    : Insets(vertical, horizontal, vertical, horizontal) {}

Insets::Insets(int top, int left, int bottom, int right)
    : top_(top), left_(left), bottom_(bottom), right_(right) {}

Insets::~Insets() {}

std::string Insets::ToString() const {
  // Print members in the same order of the constructor parameters.
  return base::StringPrintf("%d,%d,%d,%d", top(),  left(), bottom(), right());
}

}  // namespace gfx
