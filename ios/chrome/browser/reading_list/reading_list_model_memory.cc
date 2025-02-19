// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ios/chrome/browser/reading_list/reading_list_model_memory.h"
#include "ios/chrome/browser/reading_list/reading_list_model_storage.h"

#include "url/gurl.h"

ReadingListModelMemory::ReadingListModelMemory()
    : ReadingListModelMemory(NULL) {}

ReadingListModelMemory::ReadingListModelMemory(
    std::unique_ptr<ReadingListModelStorage> storage)
    : hasUnseen_(false) {
  if (storage) {
    storageLayer_ = std::move(storage);
    read_ = storageLayer_->LoadPersistentReadList();
    unread_ = storageLayer_->LoadPersistentUnreadList();
    hasUnseen_ = storageLayer_->LoadPersistentHasUnseen();
  }
  loaded_ = true;
}
ReadingListModelMemory::~ReadingListModelMemory() {}

void ReadingListModelMemory::Shutdown() {
  FOR_EACH_OBSERVER(ReadingListModelObserver, observers_,
                    ReadingListModelBeingDeleted(this));
  loaded_ = false;
}

bool ReadingListModelMemory::loaded() const {
  return loaded_;
}

size_t ReadingListModelMemory::unread_size() const {
  DCHECK(loaded());
  return unread_.size();
}

size_t ReadingListModelMemory::read_size() const {
  DCHECK(loaded());
  return read_.size();
}

bool ReadingListModelMemory::HasUnseenEntries() const {
  DCHECK(loaded());
  return unread_size() && hasUnseen_;
}

void ReadingListModelMemory::ResetUnseenEntries() {
  DCHECK(loaded());
  hasUnseen_ = false;
  if (storageLayer_ && !IsPerformingBatchUpdates()) {
    storageLayer_->SavePersistentHasUnseen(false);
  }
}

// Returns a specific entry.
const ReadingListEntry& ReadingListModelMemory::GetUnreadEntryAtIndex(
    size_t index) const {
  DCHECK(loaded());
  return unread_[index];
}
const ReadingListEntry& ReadingListModelMemory::GetReadEntryAtIndex(
    size_t index) const {
  DCHECK(loaded());
  return read_[index];
}

void ReadingListModelMemory::RemoveEntryByUrl(const GURL& url) {
  DCHECK(loaded());
  const ReadingListEntry entry(url, std::string());

  auto result = std::find(unread_.begin(), unread_.end(), entry);
  if (result != unread_.end()) {
    FOR_EACH_OBSERVER(ReadingListModelObserver, observers_,
                      ReadingListWillRemoveUnreadEntry(
                          this, std::distance(unread_.begin(), result)));
    unread_.erase(result);
    if (storageLayer_ && !IsPerformingBatchUpdates()) {
      storageLayer_->SavePersistentUnreadList(unread_);
    }
    FOR_EACH_OBSERVER(ReadingListModelObserver, observers_,
                      ReadingListDidApplyChanges(this));
    return;
  }

  result = std::find(read_.begin(), read_.end(), entry);
  if (result != read_.end()) {
    FOR_EACH_OBSERVER(ReadingListModelObserver, observers_,
                      ReadingListWillRemoveReadEntry(
                          this, std::distance(read_.begin(), result)));
    read_.erase(result);
    if (storageLayer_ && !IsPerformingBatchUpdates()) {
      storageLayer_->SavePersistentReadList(read_);
    }
    FOR_EACH_OBSERVER(ReadingListModelObserver, observers_,
                      ReadingListDidApplyChanges(this));
    return;
  }
}

const ReadingListEntry& ReadingListModelMemory::AddEntry(
    const GURL& url,
    const std::string& title) {
  DCHECK(loaded());
  RemoveEntryByUrl(url);
  const ReadingListEntry entry(url, title);
  FOR_EACH_OBSERVER(ReadingListModelObserver, observers_,
                    ReadingListWillAddUnreadEntry(this, entry));
  unread_.insert(unread_.begin(), entry);
  hasUnseen_ = true;
  if (storageLayer_ && !IsPerformingBatchUpdates()) {
    storageLayer_->SavePersistentUnreadList(unread_);
    storageLayer_->SavePersistentHasUnseen(true);
  }
  FOR_EACH_OBSERVER(ReadingListModelObserver, observers_,
                    ReadingListDidApplyChanges(this));

  return *unread_.begin();
}

void ReadingListModelMemory::MarkReadByURL(const GURL& url) {
  DCHECK(loaded());
  const ReadingListEntry entry(url, std::string());

  auto result = std::find(unread_.begin(), unread_.end(), entry);
  if (result == unread_.end()) {
    return;
  }
  FOR_EACH_OBSERVER(ReadingListModelObserver, observers_,
                    ReadingListWillRemoveUnreadEntry(
                        this, std::distance(unread_.begin(), result)));
  FOR_EACH_OBSERVER(ReadingListModelObserver, observers_,
                    ReadingListWillAddReadEntry(this, entry));

  read_.insert(read_.begin(), *result);
  unread_.erase(result);
  if (storageLayer_ && !IsPerformingBatchUpdates()) {
    storageLayer_->SavePersistentUnreadList(unread_);
    storageLayer_->SavePersistentReadList(read_);
  }

  FOR_EACH_OBSERVER(ReadingListModelObserver, observers_,
                    ReadingListDidApplyChanges(this));
}

void ReadingListModelMemory::EndBatchUpdates() {
  ReadingListModel::EndBatchUpdates();
  if (IsPerformingBatchUpdates() || !storageLayer_) {
    return;
  }
  storageLayer_->SavePersistentUnreadList(unread_);
  storageLayer_->SavePersistentReadList(read_);
  storageLayer_->SavePersistentHasUnseen(hasUnseen_);
}
