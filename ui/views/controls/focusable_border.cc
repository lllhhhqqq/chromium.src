// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/controls/focusable_border.h"

#include "third_party/skia/include/core/SkPaint.h"
#include "third_party/skia/include/core/SkPath.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/skia_util.h"
#include "ui/native_theme/native_theme.h"

namespace {

const int kInsetSize = 1;

}  // namespace

namespace views {

FocusableBorder::FocusableBorder()
    : insets_(kInsetSize, kInsetSize, kInsetSize, kInsetSize),
      override_color_(SK_ColorWHITE),
      use_default_color_(true) {
}

FocusableBorder::~FocusableBorder() {
}

void FocusableBorder::SetColor(SkColor color) {
  override_color_ = color;
  use_default_color_ = false;
}

void FocusableBorder::UseDefaultColor() {
  use_default_color_ = true;
}

void FocusableBorder::Paint(const View& view, gfx::Canvas* canvas) {
  SkPath path;
  path.addRect(gfx::RectToSkRect(view.GetLocalBounds()), SkPath::kCW_Direction);
  SkPaint paint;
  paint.setStyle(SkPaint::kStroke_Style);

  paint.setColor(GetCurrentColor(view));
  paint.setStrokeWidth(SkIntToScalar(2));

  canvas->DrawPath(path, paint);
}

gfx::Insets FocusableBorder::GetInsets() const {
  return insets_;
}

gfx::Size FocusableBorder::GetMinimumSize() const {
  return gfx::Size();
}

void FocusableBorder::SetInsets(int top, int left, int bottom, int right) {
  insets_.Set(top, left, bottom, right);
}

SkColor FocusableBorder::GetCurrentColor(const View& view) const {
  if (!use_default_color_)
    return override_color_;
  return view.GetNativeTheme()->GetSystemColor(
      view.HasFocus() ? ui::NativeTheme::kColorId_FocusedBorderColor :
                        ui::NativeTheme::kColorId_UnfocusedBorderColor);
}

}  // namespace views
