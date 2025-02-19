// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_VIEWS_STYLE_MAC_COMBOBOX_BACKGROUND_MAC_H_
#define UI_VIEWS_STYLE_MAC_COMBOBOX_BACKGROUND_MAC_H_

#include "base/macros.h"
#include "ui/views/background.h"

namespace gfx {
class Canvas;
}

namespace views {

// This class implements the background of comboboxes. Comboboxes look like
// this:
//   ---------------------------
//   | Option Text... | Arrows |
//   ---------------------------
// This class colors the background of the arrows section in accordance with our
// Mac look and feel.
class ComboboxBackgroundMac : public Background {
 public:
  ComboboxBackgroundMac();
  ~ComboboxBackgroundMac() override;

  // Background:
  void Paint(gfx::Canvas* canvas, View* view) const override;
 private:
  DISALLOW_COPY_AND_ASSIGN(ComboboxBackgroundMac);
};

}  // namespace views

#endif  // UI_VIEWS_STYLE_MAC_COMBOBOX_BACKGROUND_MAC_H_
