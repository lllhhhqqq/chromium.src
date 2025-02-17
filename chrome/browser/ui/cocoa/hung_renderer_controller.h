// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_COCOA_HUNG_RENDERER_CONTROLLER_H_
#define CHROME_BROWSER_UI_COCOA_HUNG_RENDERER_CONTROLLER_H_

// A controller for the Mac hung renderer dialog window.  Only one
// instance of this controller can exist at any time, although a given
// controller is destroyed when its window is closed.
//
// The dialog itself displays a list of frozen tabs, all of which
// share a render process.  Since there can only be a single dialog
// open at a time, if showForWebContents is called for a different
// tab, the dialog is repurposed to show a warning for the new tab.
//
// The caller is required to call endForWebContents before deleting
// any WebContents object.

#import <Cocoa/Cocoa.h>

#import "base/mac/scoped_nsobject.h"
#import "base/memory/scoped_ptr.h"

@class MultiKeyEquivalentButton;
class HungRendererWebContentsObserverBridge;

namespace content {
class WebContents;
}

@interface HungRendererController : NSWindowController<NSTableViewDataSource> {
 @private
  IBOutlet MultiKeyEquivalentButton* waitButton_;
  IBOutlet NSButton* killButton_;
  IBOutlet NSTableView* tableView_;
  IBOutlet NSImageView* imageView_;
  IBOutlet NSTextField* messageView_;

  // The WebContents for which this dialog is open.  Should never be
  // NULL while this dialog is open.
  content::WebContents* hungContents_;

  // Observes |hungContents_| in case it closes while the panel is up.
  std::unique_ptr<HungRendererWebContentsObserverBridge> hungContentsObserver_;

  // Backing data for |tableView_|.  Titles of each WebContents that
  // shares a renderer process with |hungContents_|.
  base::scoped_nsobject<NSArray> hungTitles_;

  // Favicons of each WebContents that shares a renderer process with
  // |hungContents_|.
  base::scoped_nsobject<NSArray> hungFavicons_;
}

// Shows or hides the hung renderer dialog for the given WebContents.
+ (void)showForWebContents:(content::WebContents*)contents;
+ (void)endForWebContents:(content::WebContents*)contents;

// Kills the hung renderers.
- (IBAction)kill:(id)sender;

// Waits longer for the renderers to respond.
- (IBAction)wait:(id)sender;

@end  // HungRendererController


@interface HungRendererController (JustForTesting)
- (NSButton*)killButton;
- (MultiKeyEquivalentButton*)waitButton;
@end

#endif  // CHROME_BROWSER_UI_COCOA_HUNG_RENDERER_CONTROLLER_H_
