// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/mus/ws/user_id_tracker.h"

#include "components/mus/ws/user_id_tracker_observer.h"
#include "mojo/shell/public/interfaces/connector.mojom.h"

namespace mus {
namespace ws {

UserIdTracker::UserIdTracker() : active_id_(mojo::shell::mojom::kRootUserID) {
  ids_.insert(active_id_);
}
UserIdTracker::~UserIdTracker() {
}

bool UserIdTracker::IsValidUserId(const UserId& id) const {
  return ids_.count(id) > 0;
}

void UserIdTracker::SetActiveUserId(const UserId& id) {
  if (id == active_id_)
    return;

  const UserId previously_active_id = active_id_;
  active_id_ = id;
  FOR_EACH_OBSERVER(UserIdTrackerObserver, observers_,
                    OnActiveUserIdChanged(previously_active_id, id));
}

void UserIdTracker::AddUserId(const UserId& id) {
  if (IsValidUserId(id))
    return;

  ids_.insert(id);
  FOR_EACH_OBSERVER(UserIdTrackerObserver, observers_, OnUserIdAdded(id));
}

void UserIdTracker::RemoveUserId(const UserId& id) {
  DCHECK(IsValidUserId(id));
  ids_.erase(id);
  FOR_EACH_OBSERVER(UserIdTrackerObserver, observers_, OnUserIdRemoved(id));
}

void UserIdTracker::AddObserver(UserIdTrackerObserver* observer) {
  observers_.AddObserver(observer);
}

void UserIdTracker::RemoveObserver(UserIdTrackerObserver* observer) {
  observers_.RemoveObserver(observer);
}

void UserIdTracker::Bind(mojom::UserAccessManagerRequest request) {
  bindings_.AddBinding(this, std::move(request));
}

void UserIdTracker::SetActiveUser(const mojo::String& user_id) {
  if (!IsValidUserId(user_id))
    AddUserId(user_id);
  SetActiveUserId(user_id);
}

}  // namespace ws
}  // namespace mus
