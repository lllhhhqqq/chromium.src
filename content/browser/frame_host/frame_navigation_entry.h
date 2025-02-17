// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_FRAME_HOST_FRAME_NAVIGATION_ENTRY_H_
#define CONTENT_BROWSER_FRAME_HOST_FRAME_NAVIGATION_ENTRY_H_

#include <stdint.h>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "content/browser/site_instance_impl.h"
#include "content/public/common/page_state.h"
#include "content/public/common/referrer.h"

namespace content {

// Represents a session history item for a particular frame.
//
// This class is refcounted and can be shared across multiple NavigationEntries.
// For now, it is owned by a single NavigationEntry and only tracks the main
// frame.
//
// If SiteIsolationPolicy::UseSubframeNavigationEntries is true, there will be a
// tree of FrameNavigationEntries in each NavigationEntry, one per frame.
// TODO(creis): Share these FrameNavigationEntries across NavigationEntries if
// the frame hasn't changed.
class CONTENT_EXPORT FrameNavigationEntry
    : public base::RefCounted<FrameNavigationEntry> {
 public:
  explicit FrameNavigationEntry(int frame_tree_node_id);
  FrameNavigationEntry(int frame_tree_node_id,
                       const std::string& frame_unique_name,
                       int64_t item_sequence_number,
                       int64_t document_sequence_number,
                       scoped_refptr<SiteInstanceImpl> site_instance,
                       const GURL& url,
                       const Referrer& referrer);

  // Creates a copy of this FrameNavigationEntry that can be modified
  // independently from the original.
  FrameNavigationEntry* Clone() const;

  // Updates all the members of this entry.
  void UpdateEntry(const std::string& frame_unique_name,
                   int64_t item_sequence_number,
                   int64_t document_sequence_number,
                   SiteInstanceImpl* site_instance,
                   const GURL& url,
                   const Referrer& referrer,
                   const PageState& page_state);

  // The ID of the FrameTreeNode this entry is for.  -1 for the main frame,
  // since we don't always know the FrameTreeNode ID when creating the overall
  // NavigationEntry.
  // TODO(creis): Consider removing |frame_tree_node_id| in favor of
  // |frame_unique_name|, if we can move unique name computation to the browser
  // process.
  int frame_tree_node_id() const { return frame_tree_node_id_; }
  void set_frame_tree_node_id(int frame_tree_node_id) {
    frame_tree_node_id_ = frame_tree_node_id;
  }

  // The unique name of the frame this entry is for.  This is a stable name for
  // the frame based on its position in the tree and relation to other named
  // frames, which does not change after cross-process navigations or restores.
  // Only the main frame can have an empty name.
  //
  // This is unique relative to other frames in the same page, but not among
  // other pages (i.e., not globally unique).
  const std::string& frame_unique_name() const { return frame_unique_name_; }
  void set_frame_unique_name(const std::string& frame_unique_name) {
    frame_unique_name_ = frame_unique_name;
  }

  // Keeps track of where this entry belongs in the frame's session history.
  // The item sequence number identifies each stop in the back/forward history
  // and is globally unique.  The document sequence number increments for each
  // new document and is also globally unique.  In-page navigations get a new
  // item sequence number but the same document sequence number.  These numbers
  // should not change once assigned.
  void set_item_sequence_number(int64_t item_sequence_number);
  int64_t item_sequence_number() const { return item_sequence_number_; }
  void set_document_sequence_number(int64_t document_sequence_number);
  int64_t document_sequence_number() const { return document_sequence_number_; }

  // The SiteInstance responsible for rendering this frame.  All frames sharing
  // a SiteInstance must live in the same process.  This is a refcounted pointer
  // that keeps the SiteInstance (not necessarily the process) alive as long as
  // this object remains in the session history.
  void set_site_instance(scoped_refptr<SiteInstanceImpl> site_instance) {
    site_instance_ = std::move(site_instance);
  }
  SiteInstanceImpl* site_instance() const { return site_instance_.get(); }

  // The actual URL loaded in the frame.  This is in contrast to the virtual
  // URL, which is shown to the user.
  void set_url(const GURL& url) { url_ = url; }
  const GURL& url() const { return url_; }

  // The referring URL.  Can be empty.
  void set_referrer(const Referrer& referrer) { referrer_ = referrer; }
  const Referrer& referrer() const { return referrer_; }

  void set_page_state(const PageState& page_state) { page_state_ = page_state; }
  const PageState& page_state() const { return page_state_; }

 private:
  friend class base::RefCounted<FrameNavigationEntry>;
  virtual ~FrameNavigationEntry();

  // WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING
  // Add all new fields to |UpdateEntry|.
  // TODO(creis): These fields have implications for session restore.  This is
  // currently managed by NavigationEntry, but the logic will move here.
  // WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING

  // See the accessors above for descriptions.
  int frame_tree_node_id_;
  std::string frame_unique_name_;
  int64_t item_sequence_number_;
  int64_t document_sequence_number_;
  scoped_refptr<SiteInstanceImpl> site_instance_;
  GURL url_;
  Referrer referrer_;
  // TODO(creis): Change this to FrameState.
  PageState page_state_;

  DISALLOW_COPY_AND_ASSIGN(FrameNavigationEntry);
};

}  // namespace content

#endif  // CONTENT_BROWSER_FRAME_HOST_FRAME_NAVIGATION_ENTRY_H_
