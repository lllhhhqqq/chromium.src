// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/mus/ws/accelerator.h"

#include "mojo/converters/geometry/geometry_type_converters.h"
#include "mojo/converters/input_events/input_events_type_converters.h"

namespace mus {
namespace ws {

Accelerator::Accelerator(uint32_t id, const mojom::EventMatcher& matcher)
    : id_(id),
      fields_to_match_(NONE),
      accelerator_phase_(matcher.accelerator_phase),
      event_type_(ui::ET_UNKNOWN),
      event_flags_(ui::EF_NONE),
      ignore_event_flags_(ui::EF_NONE),
      keyboard_code_(ui::VKEY_UNKNOWN),
      pointer_type_(ui::EventPointerType::POINTER_TYPE_UNKNOWN),
      weak_factory_(this) {
  if (matcher.type_matcher) {
    fields_to_match_ |= TYPE;
    switch (matcher.type_matcher->type) {
      case mus::mojom::EventType::POINTER_DOWN:
        event_type_ = ui::ET_POINTER_DOWN;
        break;
      case mus::mojom::EventType::POINTER_MOVE:
        event_type_ = ui::ET_POINTER_MOVED;
        break;
      case mus::mojom::EventType::MOUSE_EXIT:
        event_type_ = ui::ET_POINTER_EXITED;
        break;
      case mus::mojom::EventType::POINTER_UP:
        event_type_ = ui::ET_POINTER_UP;
        break;
      case mus::mojom::EventType::POINTER_CANCEL:
        event_type_ = ui::ET_POINTER_CANCELLED;
        break;
      case mus::mojom::EventType::KEY_PRESSED:
        event_type_ = ui::ET_KEY_PRESSED;
        break;
      case mus::mojom::EventType::KEY_RELEASED:
        event_type_ = ui::ET_KEY_RELEASED;
        break;
      default:
        NOTREACHED();
    }
  }
  if (matcher.flags_matcher) {
    fields_to_match_ |= FLAGS;
    event_flags_ = matcher.flags_matcher->flags;
    if (matcher.ignore_flags_matcher)
      ignore_event_flags_ = matcher.ignore_flags_matcher->flags;
  }
  if (matcher.key_matcher) {
    fields_to_match_ |= KEYBOARD_CODE;
    keyboard_code_ = static_cast<uint16_t>(matcher.key_matcher->keyboard_code);
  }
  if (matcher.pointer_kind_matcher) {
    fields_to_match_ |= POINTER_KIND;
    switch (matcher.pointer_kind_matcher->pointer_kind) {
      case mojom::PointerKind::MOUSE:
        pointer_type_ = ui::EventPointerType::POINTER_TYPE_MOUSE;
        break;
      case mojom::PointerKind::TOUCH:
        pointer_type_ = ui::EventPointerType::POINTER_TYPE_TOUCH;
        break;
      default:
        NOTREACHED();
    }
  }
  if (matcher.pointer_location_matcher) {
    fields_to_match_ |= POINTER_LOCATION;
    pointer_region_ = matcher.pointer_location_matcher->region.To<gfx::RectF>();
  }
}

Accelerator::~Accelerator() {}

bool Accelerator::MatchesEvent(const ui::Event& event,
                               const mojom::AcceleratorPhase phase) const {
  if (accelerator_phase_ != phase)
    return false;
  if ((fields_to_match_ & TYPE) && event.type() != event_type_)
    return false;
  int flags = event.flags() & ~ignore_event_flags_;
  if ((fields_to_match_ & FLAGS) && flags != event_flags_)
    return false;
  if (fields_to_match_ & KEYBOARD_CODE) {
    if (!event.IsKeyEvent())
      return false;
    if (keyboard_code_ != event.AsKeyEvent()->GetConflatedWindowsKeyCode())
      return false;
  }
  if (fields_to_match_ & POINTER_KIND) {
    if (!event.IsPointerEvent() ||
        pointer_type_ != event.AsPointerEvent()->pointer_details().pointer_type)
      return false;
  }
  if (fields_to_match_ & POINTER_LOCATION) {
    // TODO(sad): The tricky part here is to make sure the same coord-space is
    // used for the location-region and the event-location.
    NOTIMPLEMENTED();
    return false;
  }

  return true;
}

bool Accelerator::EqualEventMatcher(const Accelerator* other) const {
  return fields_to_match_ == other->fields_to_match_ &&
         accelerator_phase_ == other->accelerator_phase_ &&
         event_type_ == other->event_type_ &&
         event_flags_ == other->event_flags_ &&
         ignore_event_flags_ == other->ignore_event_flags_ &&
         keyboard_code_ == other->keyboard_code_ &&
         pointer_type_ == other->pointer_type_ &&
         pointer_region_ == other->pointer_region_;
}

}  // namespace ws
}  // namespace mus
