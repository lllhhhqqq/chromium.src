// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_GFX_MAC_NSWINDOW_FRAME_CONTROLS_H_
#define UI_GFX_MAC_NSWINDOW_FRAME_CONTROLS_H_

#include "ui/gfx/gfx_export.h"

@class NSWindow;

namespace gfx {

class Size;

// Set whether the window can be fullscreened.
GFX_EXPORT void SetNSWindowCanFullscreen(NSWindow* window,
                                         bool allow_fullscreen);

// Returns whether the window is always-on-top.
GFX_EXPORT bool IsNSWindowAlwaysOnTop(NSWindow* window);

// Sets whether the window is always-on-top. This also sets
// NSWindowCollectionBehaviorManaged because by default always-on-top windows
// are NSWindowCollectionBehaviorTransient which means they will not appear in
// Expose and cannot be moved between workspaces.
GFX_EXPORT void SetNSWindowAlwaysOnTop(NSWindow* window,
                                       bool always_on_top);

// Sets whether the window appears on all workspaces.
GFX_EXPORT void SetNSWindowVisibleOnAllWorkspaces(NSWindow* window,
                                                  bool always_visible);

// Sets the min/max size of the window as well as showing/hiding resize,
// maximize, and fullscreen controls.
// Sizes refer to the content size (inner bounds).
GFX_EXPORT void ApplyNSWindowSizeConstraints(NSWindow* window,
                                             const gfx::Size& min_size,
                                             const gfx::Size& max_size,
                                             bool can_resize,
                                             bool can_fullscreen);
GFX_EXPORT void SetNSWindowShowInTaskbar(NSWindow* window, bool show);

}  // namespace gfx

#endif  // UI_GFX_MAC_NSWINDOW_FRAME_CONTROLS_H_
