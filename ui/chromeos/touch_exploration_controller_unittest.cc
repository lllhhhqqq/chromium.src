// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/chromeos/touch_exploration_controller.h"

#include <stddef.h>

#include "base/macros.h"
#include "base/test/simple_test_tick_clock.h"
#include "base/time/time.h"
#include "ui/aura/client/cursor_client.h"
#include "ui/aura/test/aura_test_base.h"
#include "ui/aura/test/test_cursor_client.h"
#include "ui/aura/window.h"
#include "ui/events/event.h"
#include "ui/events/event_utils.h"
#include "ui/events/gestures/gesture_provider_aura.h"
#include "ui/events/test/event_generator.h"
#include "ui/events/test/events_test_utils.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gl/gl_implementation.h"
#include "ui/gl/test/gl_surface_test_support.h"

namespace ui {

namespace {

// Records all mouse, touch, gesture, and key events.
class EventCapturer : public ui::EventHandler {
 public:
  EventCapturer() {}
  ~EventCapturer() override {}

  void Reset() {
    events_.clear();
  }

  void OnEvent(ui::Event* event) override {
    if (event->IsMouseEvent() || event->IsTouchEvent() ||
        event->IsGestureEvent() || event->IsKeyEvent()) {
      events_.push_back(ui::Event::Clone(*event));
    } else {
      return;
    }
    // Stop event propagation so we don't click on random stuff that
    // might break test assumptions.
    event->StopPropagation();
    // If there is a possibility that we're in an infinite loop, we should
    // exit early with a sensible error rather than letting the test time out.
    ASSERT_LT(events_.size(), 100u);
  }

  const ScopedVector<ui::Event>& captured_events() const { return events_; }

 private:
  ScopedVector<ui::Event> events_;

  DISALLOW_COPY_AND_ASSIGN(EventCapturer);
};

int Factorial(int n) {
  if (n <= 0)
    return 0;
  if (n == 1)
    return 1;
  return n * Factorial(n - 1);
}

class MockTouchExplorationControllerDelegate
    : public ui::TouchExplorationControllerDelegate {
 public:
  void SetOutputLevel(int volume) override {
    volume_changes_.push_back(volume);
  }
  void SilenceSpokenFeedback() override {}
  void PlayVolumeAdjustEarcon() override { ++num_times_adjust_sound_played_; }
  void PlayPassthroughEarcon() override { ++num_times_passthrough_played_; }
  void PlayExitScreenEarcon() override { ++num_times_exit_screen_played_; }
  void PlayEnterScreenEarcon() override { ++num_times_enter_screen_played_; }

  const std::vector<float> VolumeChanges() { return volume_changes_; }
  size_t NumAdjustSounds() { return num_times_adjust_sound_played_; }
  size_t NumPassthroughSounds() { return num_times_passthrough_played_; }
  size_t NumExitScreenSounds() { return num_times_exit_screen_played_; }
  size_t NumEnterScreenSounds() {
    return num_times_enter_screen_played_;
  }

  void ResetCountersToZero() {
    num_times_adjust_sound_played_ = 0;
    num_times_passthrough_played_ = 0;
    num_times_exit_screen_played_ = 0;
    num_times_enter_screen_played_ = 0;
  }

 private:
  std::vector<float> volume_changes_;
  size_t num_times_adjust_sound_played_ = 0;
  size_t num_times_passthrough_played_ = 0;
  size_t num_times_exit_screen_played_ = 0;
  size_t num_times_enter_screen_played_ = 0;
};

}  // namespace

class TouchExplorationControllerTestApi {
 public:
  TouchExplorationControllerTestApi(
      TouchExplorationController* touch_exploration_controller) {
    touch_exploration_controller_.reset(touch_exploration_controller);
  }

  void CallTapTimerNowForTesting() {
    DCHECK(touch_exploration_controller_->tap_timer_.IsRunning());
    touch_exploration_controller_->tap_timer_.Stop();
    touch_exploration_controller_->OnTapTimerFired();
  }

  void CallPassthroughTimerNowForTesting() {
    DCHECK(touch_exploration_controller_->passthrough_timer_.IsRunning());
    touch_exploration_controller_->passthrough_timer_.Stop();
    touch_exploration_controller_->OnPassthroughTimerFired();
  }

  void CallTapTimerNowIfRunningForTesting() {
    if (touch_exploration_controller_->tap_timer_.IsRunning()) {
      touch_exploration_controller_->tap_timer_.Stop();
      touch_exploration_controller_->OnTapTimerFired();
    }
  }

  bool IsInNoFingersDownStateForTesting() const {
    return touch_exploration_controller_->state_ ==
           touch_exploration_controller_->NO_FINGERS_DOWN;
  }

  bool IsInGestureInProgressStateForTesting() const {
    return touch_exploration_controller_->state_ ==
           touch_exploration_controller_->GESTURE_IN_PROGRESS;
  }

  bool IsInSlideGestureStateForTesting() const {
    return touch_exploration_controller_->state_ ==
           touch_exploration_controller_->SLIDE_GESTURE;
  }

  bool IsInTwoFingerTapStateForTesting() const {
    return touch_exploration_controller_->state_ ==
           touch_exploration_controller_->TWO_FINGER_TAP;
  }
  bool IsInCornerPassthroughStateForTesting() const {
    return touch_exploration_controller_->state_ ==
           touch_exploration_controller_->CORNER_PASSTHROUGH;
  }

  gfx::Rect BoundsOfRootWindowInDIPForTesting() const {
    return touch_exploration_controller_->root_window_->GetBoundsInScreen();
  }

  // VLOGs should be suppressed in tests that generate a lot of logs,
  // for example permutations of nine touch events.
  void SuppressVLOGsForTesting(bool suppress) {
    touch_exploration_controller_->VLOG_on_ = !suppress;
  }

  float GetMaxDistanceFromEdge() const {
    return touch_exploration_controller_->kMaxDistanceFromEdge;
  }

  float GetSlopDistanceFromEdge() const {
    return touch_exploration_controller_->kSlopDistanceFromEdge;
  }

  void SetTickClockForTesting(base::TickClock* simulated_clock) {
    touch_exploration_controller_->tick_clock_ = simulated_clock;
  }

 private:
  scoped_ptr<TouchExplorationController> touch_exploration_controller_;

  DISALLOW_COPY_AND_ASSIGN(TouchExplorationControllerTestApi);
};

class TouchExplorationTest : public aura::test::AuraTestBase {
 public:
  TouchExplorationTest() : simulated_clock_(new base::SimpleTestTickClock()) {
    // Tests fail if time is ever 0.
    simulated_clock_->Advance(base::TimeDelta::FromMilliseconds(10));
  }
  ~TouchExplorationTest() override {}

  void SetUp() override {
    if (gfx::GetGLImplementation() == gfx::kGLImplementationNone)
      gfx::GLSurfaceTestSupport::InitializeOneOff();
    aura::test::AuraTestBase::SetUp();
    cursor_client_.reset(new aura::test::TestCursorClient(root_window()));
    root_window()->AddPreTargetHandler(&event_capturer_);
    generator_.reset(new test::EventGenerator(root_window()));
    // The generator takes ownership of the tick clock.
    generator_->SetTickClock(scoped_ptr<base::TickClock>(simulated_clock_));
    cursor_client()->ShowCursor();
    cursor_client()->DisableMouseEvents();
  }

  void TearDown() override {
    root_window()->RemovePreTargetHandler(&event_capturer_);
    SwitchTouchExplorationMode(false);
    cursor_client_.reset();
    aura::test::AuraTestBase::TearDown();
  }

 protected:
  aura::client::CursorClient* cursor_client() { return cursor_client_.get(); }

  const ScopedVector<ui::Event>& GetCapturedEvents() {
    return event_capturer_.captured_events();
  }

  std::vector<ui::LocatedEvent*> GetCapturedLocatedEvents() {
    const ScopedVector<ui::Event>& all_events = GetCapturedEvents();
    std::vector<ui::LocatedEvent*> located_events;
    for (size_t i = 0; i < all_events.size(); ++i) {
      if (all_events[i]->IsMouseEvent() ||
          all_events[i]->IsTouchEvent() ||
          all_events[i]->IsGestureEvent()) {
        located_events.push_back(static_cast<ui::LocatedEvent*>(all_events[i]));
      }
    }
    return located_events;
  }

  std::vector<ui::Event*> GetCapturedEventsOfType(int type) {
    const ScopedVector<ui::Event>& all_events = GetCapturedEvents();
    std::vector<ui::Event*> events;
    for (size_t i = 0; i < all_events.size(); ++i) {
      if (type == all_events[i]->type())
        events.push_back(all_events[i]);
    }
    return events;
  }

  std::vector<ui::LocatedEvent*> GetCapturedLocatedEventsOfType(int type) {
    std::vector<ui::LocatedEvent*> located_events = GetCapturedLocatedEvents();
    std::vector<ui::LocatedEvent*> events;
    for (size_t i = 0; i < located_events.size(); ++i) {
      if (type == located_events[i]->type())
        events.push_back(located_events[i]);
    }
    return events;
  }

  void ClearCapturedEvents() {
    event_capturer_.Reset();
  }

  void AdvanceSimulatedTimePastTapDelay() {
    simulated_clock_->Advance(gesture_detector_config_.double_tap_timeout);
    simulated_clock_->Advance(base::TimeDelta::FromMilliseconds(1));
    touch_exploration_controller_->CallTapTimerNowForTesting();
  }

  void AdvanceSimulatedTimePastPassthroughDelay() {
    simulated_clock_->Advance(base::TimeDelta::FromMilliseconds(1000));
    touch_exploration_controller_->CallPassthroughTimerNowForTesting();
  }

  void AdvanceSimulatedTimePastPotentialTapDelay() {
    simulated_clock_->Advance(base::TimeDelta::FromMilliseconds(1000));
    touch_exploration_controller_->CallTapTimerNowIfRunningForTesting();
  }

  void SuppressVLOGs(bool suppress) {
    touch_exploration_controller_->SuppressVLOGsForTesting(suppress);
  }

  void SetTickClock() {
    touch_exploration_controller_->SetTickClockForTesting(
        static_cast<base::TickClock*>(simulated_clock_));
  }

  void SwitchTouchExplorationMode(bool on) {
    if (!on && touch_exploration_controller_.get()) {
      touch_exploration_controller_.reset();
    } else if (on && !touch_exploration_controller_.get()) {
      touch_exploration_controller_.reset(
          new ui::TouchExplorationControllerTestApi(
              new TouchExplorationController(root_window(), &delegate_)));
      cursor_client()->ShowCursor();
      cursor_client()->DisableMouseEvents();
    }
  }

  void EnterTouchExplorationModeAtLocation(gfx::Point tap_location) {
    ui::TouchEvent touch_press(ui::ET_TOUCH_PRESSED, tap_location, 0, Now());
    generator_->Dispatch(&touch_press);
    AdvanceSimulatedTimePastTapDelay();
    EXPECT_TRUE(IsInTouchToMouseMode());
  }

  // Checks that Corner Passthrough is working. Assumes that corner is the
  // bottom left corner or the bottom right corner.
  void AssertCornerPassthroughWorking(gfx::Point corner) {
    ASSERT_EQ(0U, delegate_.NumPassthroughSounds());

    ui::TouchEvent first_press(ui::ET_TOUCH_PRESSED, corner, 0, Now());
    generator_->Dispatch(&first_press);

    AdvanceSimulatedTimePastPassthroughDelay();
    EXPECT_FALSE(IsInGestureInProgressState());
    EXPECT_FALSE(IsInSlideGestureState());
    EXPECT_FALSE(IsInTouchToMouseMode());
    EXPECT_TRUE(IsInCornerPassthroughState());

    gfx::Rect window = BoundsOfRootWindowInDIP();
    // The following events should be passed through.
    gfx::Point passthrough(window.right() / 2, window.bottom() / 2);
    ui::TouchEvent passthrough_press(
        ui::ET_TOUCH_PRESSED, passthrough, 1, Now());
    ASSERT_EQ(1U, delegate_.NumPassthroughSounds());
    generator_->Dispatch(&passthrough_press);
    generator_->ReleaseTouchId(1);
    generator_->PressTouchId(1);
    EXPECT_FALSE(IsInGestureInProgressState());
    EXPECT_FALSE(IsInSlideGestureState());
    EXPECT_TRUE(IsInCornerPassthroughState());

    std::vector<ui::LocatedEvent*> captured_events = GetCapturedLocatedEvents();
    ASSERT_EQ(3U, captured_events.size());
    EXPECT_EQ(ui::ET_TOUCH_PRESSED, captured_events[0]->type());
    EXPECT_EQ(ui::ET_TOUCH_RELEASED, captured_events[1]->type());
    EXPECT_EQ(ui::ET_TOUCH_PRESSED, captured_events[2]->type());
    generator_->ReleaseTouchId(1);
    ClearCapturedEvents();

    generator_->ReleaseTouchId(0);
    captured_events = GetCapturedLocatedEvents();
    ASSERT_EQ(0U, captured_events.size());
    EXPECT_FALSE(IsInTouchToMouseMode());
    EXPECT_FALSE(IsInCornerPassthroughState());
    ClearCapturedEvents();
  }

  bool IsInTouchToMouseMode() {
    aura::client::CursorClient* cursor_client =
        aura::client::GetCursorClient(root_window());
    return cursor_client &&
           cursor_client->IsMouseEventsEnabled() &&
           !cursor_client->IsCursorVisible();
  }

  bool IsInNoFingersDownState() {
    return touch_exploration_controller_->IsInNoFingersDownStateForTesting();
  }

  bool IsInGestureInProgressState() {
    return touch_exploration_controller_
        ->IsInGestureInProgressStateForTesting();
  }

  bool IsInSlideGestureState() {
    return touch_exploration_controller_->IsInSlideGestureStateForTesting();
  }

  bool IsInTwoFingerTapState() {
    return touch_exploration_controller_->IsInTwoFingerTapStateForTesting();
  }

  bool IsInCornerPassthroughState() {
    return touch_exploration_controller_
        ->IsInCornerPassthroughStateForTesting();
  }

  gfx::Rect BoundsOfRootWindowInDIP() {
    return touch_exploration_controller_->BoundsOfRootWindowInDIPForTesting();
  }

  float GetMaxDistanceFromEdge() const {
    return touch_exploration_controller_->GetMaxDistanceFromEdge();
  }

  float GetSlopDistanceFromEdge() const {
    return touch_exploration_controller_->GetSlopDistanceFromEdge();
  }

  base::TimeDelta Now() {
    // This is the same as what EventTimeForNow() does, but here we do it
    // with our simulated clock.
    return base::TimeDelta::FromInternalValue(
        simulated_clock_->NowTicks().ToInternalValue());
  }

  scoped_ptr<test::EventGenerator> generator_;
  ui::GestureDetector::Config gesture_detector_config_;
  // Owned by |generator_|.
  base::SimpleTestTickClock* simulated_clock_;
  MockTouchExplorationControllerDelegate delegate_;

 private:
  EventCapturer event_capturer_;
  scoped_ptr<TouchExplorationControllerTestApi>
      touch_exploration_controller_;
  scoped_ptr<aura::test::TestCursorClient> cursor_client_;

  DISALLOW_COPY_AND_ASSIGN(TouchExplorationTest);
};

// Executes a number of assertions to confirm that |e1| and |e2| are touch
// events and are equal to each other.
void ConfirmEventsAreTouchAndEqual(ui::Event* e1, ui::Event* e2) {
  ASSERT_TRUE(e1->IsTouchEvent());
  ASSERT_TRUE(e2->IsTouchEvent());
  ui::TouchEvent* touch_event1 = e1->AsTouchEvent();
  ui::TouchEvent* touch_event2 = e2->AsTouchEvent();
  EXPECT_EQ(touch_event1->type(), touch_event2->type());
  EXPECT_EQ(touch_event1->location(), touch_event2->location());
  EXPECT_EQ(touch_event1->touch_id(), touch_event2->touch_id());
  EXPECT_EQ(touch_event1->flags(), touch_event2->flags());
  EXPECT_EQ(touch_event1->time_stamp(), touch_event2->time_stamp());
}

// Executes a number of assertions to confirm that |e1| and |e2| are mouse
// events and are equal to each other.
void ConfirmEventsAreMouseAndEqual(ui::Event* e1, ui::Event* e2) {
  ASSERT_TRUE(e1->IsMouseEvent());
  ASSERT_TRUE(e2->IsMouseEvent());
  ui::MouseEvent* mouse_event1 = e1->AsMouseEvent();
  ui::MouseEvent* mouse_event2 = e2->AsMouseEvent();
  EXPECT_EQ(mouse_event1->type(), mouse_event2->type());
  EXPECT_EQ(mouse_event1->location(), mouse_event2->location());
  EXPECT_EQ(mouse_event1->root_location(), mouse_event2->root_location());
  EXPECT_EQ(mouse_event1->flags(), mouse_event2->flags());
}

// Executes a number of assertions to confirm that |e1| and |e2| are key events
// and are equal to each other.
void ConfirmEventsAreKeyAndEqual(ui::Event* e1, ui::Event* e2) {
  ASSERT_TRUE(e1->IsKeyEvent());
  ASSERT_TRUE(e2->IsKeyEvent());
  ui::KeyEvent* key_event1 = e1->AsKeyEvent();
  ui::KeyEvent* key_event2 = e2->AsKeyEvent();
  EXPECT_EQ(key_event1->type(), key_event2->type());
  EXPECT_EQ(key_event1->key_code(), key_event2->key_code());
  EXPECT_EQ(key_event1->code(), key_event2->code());
  EXPECT_EQ(key_event1->flags(), key_event2->flags());
}

#define CONFIRM_EVENTS_ARE_TOUCH_AND_EQUAL(e1, e2) \
  ASSERT_NO_FATAL_FAILURE(ConfirmEventsAreTouchAndEqual(e1, e2))

#define CONFIRM_EVENTS_ARE_MOUSE_AND_EQUAL(e1, e2) \
  ASSERT_NO_FATAL_FAILURE(ConfirmEventsAreMouseAndEqual(e1, e2))

#define CONFIRM_EVENTS_ARE_KEY_AND_EQUAL(e1, e2) \
  ASSERT_NO_FATAL_FAILURE(ConfirmEventsAreKeyAndEqual(e1, e2))

// TODO(mfomitchev): Need to investigate why we don't get mouse enter/exit
// events when running these tests as part of ui_base_unittests. We do get them
// when the tests are run as part of ash unit tests.

// If a swipe has been successfully completed, then six key events will be
// dispatched that correspond to shift+search+direction
void AssertDirectionalNavigationEvents(const ScopedVector<ui::Event>& events,
                                       ui::KeyboardCode direction) {
  ASSERT_EQ(6U, events.size());
  ui::KeyEvent shift_pressed(
      ui::ET_KEY_PRESSED, ui::VKEY_SHIFT, ui::EF_SHIFT_DOWN);
  ui::KeyEvent search_pressed(
      ui::ET_KEY_PRESSED, ui::VKEY_LWIN, ui::EF_SHIFT_DOWN);
  ui::KeyEvent direction_pressed(
      ui::ET_KEY_PRESSED, direction, ui::EF_SHIFT_DOWN);
  ui::KeyEvent direction_released(
      ui::ET_KEY_RELEASED, direction, ui::EF_SHIFT_DOWN);
  ui::KeyEvent search_released(
      ui::ET_KEY_RELEASED, VKEY_LWIN, ui::EF_SHIFT_DOWN);
  ui::KeyEvent shift_released(
      ui::ET_KEY_RELEASED, ui::VKEY_SHIFT, ui::EF_NONE);
  CONFIRM_EVENTS_ARE_KEY_AND_EQUAL(&shift_pressed, events[0]);
  CONFIRM_EVENTS_ARE_KEY_AND_EQUAL(&search_pressed, events[1]);
  CONFIRM_EVENTS_ARE_KEY_AND_EQUAL(&direction_pressed, events[2]);
  CONFIRM_EVENTS_ARE_KEY_AND_EQUAL(&direction_released, events[3]);
  CONFIRM_EVENTS_ARE_KEY_AND_EQUAL(&search_released, events[4]);
  CONFIRM_EVENTS_ARE_KEY_AND_EQUAL(&shift_released, events[5]);
}

TEST_F(TouchExplorationTest, EntersTouchToMouseModeAfterPressAndDelay) {
  SwitchTouchExplorationMode(true);
  EXPECT_FALSE(IsInTouchToMouseMode());
  generator_->PressTouch();
  AdvanceSimulatedTimePastTapDelay();
  EXPECT_TRUE(IsInTouchToMouseMode());
}

TEST_F(TouchExplorationTest, EntersTouchToMouseModeAfterMoveOutsideSlop) {
  int slop = gesture_detector_config_.touch_slop;
  int half_slop = slop / 2;

  SwitchTouchExplorationMode(true);
  EXPECT_FALSE(IsInTouchToMouseMode());
  generator_->set_current_location(gfx::Point(11, 12));
  generator_->PressTouch();
  generator_->MoveTouch(gfx::Point(11 + half_slop, 12));
  EXPECT_FALSE(IsInTouchToMouseMode());
  generator_->MoveTouch(gfx::Point(11, 12 + half_slop));
  EXPECT_FALSE(IsInTouchToMouseMode());
  AdvanceSimulatedTimePastTapDelay();
  generator_->MoveTouch(gfx::Point(11 + slop + 1, 12));
  EXPECT_TRUE(IsInTouchToMouseMode());
}

TEST_F(TouchExplorationTest, OneFingerTap) {
  SwitchTouchExplorationMode(true);
  gfx::Point location(11, 12);
  generator_->set_current_location(location);
  generator_->PressTouch();
  generator_->ReleaseTouch();
  AdvanceSimulatedTimePastTapDelay();

  std::vector<ui::LocatedEvent*> events =
      GetCapturedLocatedEventsOfType(ui::ET_MOUSE_MOVED);
  ASSERT_EQ(1U, events.size());

  EXPECT_EQ(location, events[0]->location());
  EXPECT_TRUE(events[0]->flags() & ui::EF_IS_SYNTHESIZED);
  EXPECT_TRUE(events[0]->flags() & ui::EF_TOUCH_ACCESSIBILITY);
  EXPECT_TRUE(IsInNoFingersDownState());
}

TEST_F(TouchExplorationTest, ActualMouseMovesUnaffected) {
  SwitchTouchExplorationMode(true);

  gfx::Point location_start(11, 12);
  gfx::Point location_end(13, 14);
  generator_->set_current_location(location_start);
  generator_->PressTouch();
  AdvanceSimulatedTimePastTapDelay();
  generator_->MoveTouch(location_end);

  gfx::Point location_real_mouse_move(15, 16);
  ui::MouseEvent mouse_move(ui::ET_MOUSE_MOVED, location_real_mouse_move,
                            location_real_mouse_move, ui::EventTimeForNow(), 0,
                            0);
  generator_->Dispatch(&mouse_move);
  generator_->ReleaseTouch();
  AdvanceSimulatedTimePastTapDelay();

  std::vector<ui::LocatedEvent*> events =
      GetCapturedLocatedEventsOfType(ui::ET_MOUSE_MOVED);
  ASSERT_EQ(4U, events.size());

  EXPECT_EQ(location_start, events[0]->location());
  EXPECT_TRUE(events[0]->flags() & ui::EF_IS_SYNTHESIZED);
  EXPECT_TRUE(events[0]->flags() & ui::EF_TOUCH_ACCESSIBILITY);

  EXPECT_EQ(location_end, events[1]->location());
  EXPECT_TRUE(events[1]->flags() & ui::EF_IS_SYNTHESIZED);
  EXPECT_TRUE(events[1]->flags() & ui::EF_TOUCH_ACCESSIBILITY);

  // The real mouse move goes through.
  EXPECT_EQ(location_real_mouse_move, events[2]->location());
  CONFIRM_EVENTS_ARE_MOUSE_AND_EQUAL(events[2], &mouse_move);
  EXPECT_FALSE(events[2]->flags() & ui::EF_IS_SYNTHESIZED);
  EXPECT_FALSE(events[2]->flags() & ui::EF_TOUCH_ACCESSIBILITY);

  // The touch release gets written as a mouse move.
  EXPECT_EQ(location_end, events[3]->location());
  EXPECT_TRUE(events[3]->flags() & ui::EF_IS_SYNTHESIZED);
  EXPECT_TRUE(events[3]->flags() & ui::EF_TOUCH_ACCESSIBILITY);
  EXPECT_TRUE(IsInNoFingersDownState());
}

// Turn the touch exploration mode on in the middle of the touch gesture.
// Confirm that events from the finger which was touching when the mode was
// turned on don't get rewritten.
TEST_F(TouchExplorationTest, TurnOnMidTouch) {
  SwitchTouchExplorationMode(false);
  generator_->PressTouchId(1);
  EXPECT_TRUE(cursor_client()->IsCursorVisible());
  ClearCapturedEvents();

  // Enable touch exploration mode while the first finger is touching the
  // screen. Ensure that subsequent events from that first finger are not
  // affected by the touch exploration mode, while the touch events from another
  // finger get rewritten.
  SwitchTouchExplorationMode(true);
  ui::TouchEvent touch_move(ui::ET_TOUCH_MOVED,
                            gfx::Point(11, 12),
                            1,
                            Now());
  generator_->Dispatch(&touch_move);
  EXPECT_TRUE(cursor_client()->IsCursorVisible());
  EXPECT_FALSE(cursor_client()->IsMouseEventsEnabled());
  std::vector<ui::LocatedEvent*> captured_events = GetCapturedLocatedEvents();
  ASSERT_EQ(1u, captured_events.size());
  CONFIRM_EVENTS_ARE_TOUCH_AND_EQUAL(captured_events[0], &touch_move);
  ClearCapturedEvents();

  // The press from the second finger should get rewritten.
  generator_->PressTouchId(2);
  AdvanceSimulatedTimePastTapDelay();
  EXPECT_TRUE(IsInTouchToMouseMode());
  captured_events = GetCapturedLocatedEvents();
  std::vector<ui::LocatedEvent*>::const_iterator it;
  for (it = captured_events.begin(); it != captured_events.end(); ++it) {
    if ((*it)->type() == ui::ET_MOUSE_MOVED)
      break;
  }
  EXPECT_NE(captured_events.end(), it);
  ClearCapturedEvents();

  // The release of the first finger shouldn't be affected.
  ui::TouchEvent touch_release(ui::ET_TOUCH_RELEASED,
                               gfx::Point(11, 12),
                               1,
                               Now());
  generator_->Dispatch(&touch_release);
  captured_events = GetCapturedLocatedEvents();
  ASSERT_EQ(1u, captured_events.size());
  CONFIRM_EVENTS_ARE_TOUCH_AND_EQUAL(captured_events[0], &touch_release);
  ClearCapturedEvents();

  // The move and release from the second finger should get rewritten.
  generator_->MoveTouchId(gfx::Point(13, 14), 2);
  generator_->ReleaseTouchId(2);
  AdvanceSimulatedTimePastTapDelay();
  captured_events = GetCapturedLocatedEvents();
  ASSERT_EQ(2u, captured_events.size());
  EXPECT_EQ(ui::ET_MOUSE_MOVED, captured_events[0]->type());
  EXPECT_EQ(ui::ET_MOUSE_MOVED, captured_events[1]->type());
  EXPECT_TRUE(IsInNoFingersDownState());
}

// If an event is received after the double-tap timeout has elapsed, but
// before the timer has fired, a mouse move should still be generated.
TEST_F(TouchExplorationTest, TimerFiresLateDuringTouchExploration) {
  SwitchTouchExplorationMode(true);

  // Make sure the touch is not in a corner of the screen.
  generator_->MoveTouch(gfx::Point(100, 200));

  // Send a press, then add another finger after the double-tap timeout.
  generator_->PressTouchId(1);
  simulated_clock_->Advance(base::TimeDelta::FromMilliseconds(1000));
  generator_->PressTouchId(2);
  std::vector<ui::LocatedEvent*> events =
      GetCapturedLocatedEventsOfType(ui::ET_MOUSE_MOVED);
  ASSERT_EQ(1U, events.size());
  EXPECT_TRUE(events[0]->flags() & ui::EF_IS_SYNTHESIZED);
  EXPECT_TRUE(events[0]->flags() & ui::EF_TOUCH_ACCESSIBILITY);

  generator_->ReleaseTouchId(2);
  generator_->ReleaseTouchId(1);
  AdvanceSimulatedTimePastTapDelay();
  EXPECT_TRUE(IsInNoFingersDownState());
}

// If a new tap is received after the double-tap timeout has elapsed from
// a previous tap, but before the timer has fired, a mouse move should
// still be generated from the old tap.
TEST_F(TouchExplorationTest, TimerFiresLateAfterTap) {
  SwitchTouchExplorationMode(true);

  // Send a tap at location1.
  gfx::Point location0(11, 12);
  generator_->set_current_location(location0);
  generator_->PressTouch();
  generator_->ReleaseTouch();

  // Send a tap at location2, after the double-tap timeout, but before the
  // timer fires.
  gfx::Point location1(33, 34);
  generator_->set_current_location(location1);
  simulated_clock_->Advance(base::TimeDelta::FromMilliseconds(301));
  generator_->PressTouch();
  generator_->ReleaseTouch();
  AdvanceSimulatedTimePastTapDelay();

  std::vector<ui::LocatedEvent*> events =
      GetCapturedLocatedEventsOfType(ui::ET_MOUSE_MOVED);
  ASSERT_EQ(2U, events.size());
  EXPECT_EQ(location0, events[0]->location());
  EXPECT_TRUE(events[0]->flags() & ui::EF_IS_SYNTHESIZED);
  EXPECT_TRUE(events[0]->flags() & ui::EF_TOUCH_ACCESSIBILITY);
  EXPECT_EQ(location1, events[1]->location());
  EXPECT_TRUE(events[1]->flags() & ui::EF_IS_SYNTHESIZED);
  EXPECT_TRUE(events[1]->flags() & ui::EF_TOUCH_ACCESSIBILITY);
  EXPECT_TRUE(IsInNoFingersDownState());
}

// Double-tapping should send a touch press and release through to the location
// of the last successful touch exploration.
TEST_F(TouchExplorationTest, DoubleTap) {
  SwitchTouchExplorationMode(true);

  // Tap at one location, and get a mouse move event.
  gfx::Point tap_location(51, 52);
  generator_->set_current_location(tap_location);
  generator_->PressTouchId(1);
  generator_->ReleaseTouchId(1);
  AdvanceSimulatedTimePastTapDelay();

  std::vector<ui::LocatedEvent*> events =
      GetCapturedLocatedEventsOfType(ui::ET_MOUSE_MOVED);
  ASSERT_EQ(1U, events.size());

  EXPECT_EQ(tap_location, events[0]->location());
  EXPECT_TRUE(events[0]->flags() & ui::EF_IS_SYNTHESIZED);
  EXPECT_TRUE(events[0]->flags() & ui::EF_TOUCH_ACCESSIBILITY);
  ClearCapturedEvents();

  // Now double-tap at a different location. This should result in
  // a single touch press and release at the location of the tap,
  // not at the location of the double-tap.
  gfx::Point double_tap_location(33, 34);
  generator_->set_current_location(double_tap_location);
  generator_->PressTouch();
  generator_->ReleaseTouch();
  generator_->PressTouch();
  generator_->ReleaseTouch();

  std::vector<ui::LocatedEvent*> captured_events = GetCapturedLocatedEvents();
  ASSERT_EQ(2U, captured_events.size());
  EXPECT_EQ(ui::ET_TOUCH_PRESSED, captured_events[0]->type());
  EXPECT_EQ(tap_location, captured_events[0]->location());
  EXPECT_EQ(ui::ET_TOUCH_RELEASED, captured_events[1]->type());
  EXPECT_EQ(tap_location, captured_events[1]->location());
  EXPECT_TRUE(IsInNoFingersDownState());
}

// Double-tapping where the user holds their finger down for the second time
// for a longer press should send a touch press and passthrough all further
// events from that finger. Other finger presses should be ignored.
TEST_F(TouchExplorationTest, DoubleTapPassthrough) {
  SwitchTouchExplorationMode(true);

  // Tap at one location, and get a mouse move event.
  gfx::Point tap_location(11, 12);
  generator_->set_current_location(tap_location);
  generator_->PressTouch();
  generator_->ReleaseTouch();
  AdvanceSimulatedTimePastTapDelay();

  std::vector<ui::LocatedEvent*> events =
      GetCapturedLocatedEventsOfType(ui::ET_MOUSE_MOVED);
  ASSERT_EQ(1U, events.size());

  EXPECT_EQ(tap_location, events[0]->location());
  EXPECT_TRUE(events[0]->flags() & ui::EF_IS_SYNTHESIZED);
  EXPECT_TRUE(events[0]->flags() & ui::EF_TOUCH_ACCESSIBILITY);
  ClearCapturedEvents();

  // Now double-tap and hold at a different location.
  // This should result in a single touch press at the location of the tap,
  // not at the location of the double-tap.
  gfx::Point first_tap_location(13, 14);
  generator_->set_current_location(first_tap_location);
  generator_->PressTouchId(1);
  generator_->ReleaseTouchId(1);
  gfx::Point second_tap_location(15, 16);
  generator_->set_current_location(second_tap_location);
  generator_->PressTouchId(1);
  // Advance to the finger passing through.
  AdvanceSimulatedTimePastTapDelay();

  gfx::Vector2d passthrough_offset = second_tap_location - tap_location;

  std::vector<ui::LocatedEvent*> captured_events = GetCapturedLocatedEvents();
  ASSERT_EQ(1U, captured_events.size());
  EXPECT_EQ(ui::ET_TOUCH_PRESSED, captured_events[0]->type());
  EXPECT_EQ(second_tap_location - passthrough_offset,
            captured_events[0]->location());

  ClearCapturedEvents();

  // All events for the first finger should pass through now, displaced
  // relative to the last touch exploration location.
  gfx::Point first_move_location(17, 18);
  generator_->MoveTouchId(first_move_location, 1);
  gfx::Point second_move_location(12, 13);
  generator_->MoveTouchId(second_move_location, 1);

  captured_events = GetCapturedLocatedEvents();
  ASSERT_EQ(2U, captured_events.size());
  EXPECT_EQ(ui::ET_TOUCH_MOVED, captured_events[0]->type());
  EXPECT_EQ(first_move_location - passthrough_offset,
            captured_events[0]->location());
  EXPECT_EQ(ui::ET_TOUCH_MOVED, captured_events[1]->type());
  EXPECT_EQ(second_move_location - passthrough_offset,
            captured_events[1]->location());

  ClearCapturedEvents();

  // Events for other fingers should do nothing.
  generator_->PressTouchId(2);
  generator_->PressTouchId(3);
  generator_->MoveTouchId(gfx::Point(34, 36), 2);
  generator_->ReleaseTouchId(2);
  captured_events = GetCapturedLocatedEvents();
  ASSERT_EQ(0U, captured_events.size());

  // Even with finger 3 still down, events for the first finger should still
  // pass through.
  gfx::Point third_move_location(14, 15);
  generator_->MoveTouchId(third_move_location, 1);
  captured_events = GetCapturedLocatedEvents();
  ASSERT_EQ(1U, captured_events.size());
  EXPECT_EQ(ui::ET_TOUCH_MOVED, captured_events[0]->type());
  EXPECT_EQ(third_move_location - passthrough_offset,
            captured_events[0]->location());

  // No fingers down state is only reached when every finger is lifted.
  generator_->ReleaseTouchId(1);
  EXPECT_FALSE(IsInNoFingersDownState());
  generator_->ReleaseTouchId(3);
  EXPECT_TRUE(IsInNoFingersDownState());
}

// Double-tapping, going into passthrough, and holding for the longpress
// time should send a touch press and released (right click)
// to the location of the last successful touch exploration.
TEST_F(TouchExplorationTest, DoubleTapLongPress) {
  SwitchTouchExplorationMode(true);
  SetTickClock();
  // Tap at one location, and get a mouse move event.
  gfx::Point tap_location(11, 12);
  generator_->set_current_location(tap_location);
  generator_->PressTouch();
  generator_->ReleaseTouch();
  AdvanceSimulatedTimePastTapDelay();

  std::vector<ui::LocatedEvent*> events =
      GetCapturedLocatedEventsOfType(ui::ET_MOUSE_MOVED);
  ASSERT_EQ(1U, events.size());
  EXPECT_EQ(tap_location, events[0]->location());
  EXPECT_TRUE(events[0]->flags() & ui::EF_IS_SYNTHESIZED);
  EXPECT_TRUE(events[0]->flags() & ui::EF_TOUCH_ACCESSIBILITY);
  ClearCapturedEvents();

  // Now double-tap and hold at a different location.
  // This should result in a single touch long press and release
  // at the location of the tap, not at the location of the double-tap.
  // There should be a time delay between the touch press and release.
  gfx::Point first_tap_location(33, 34);
  generator_->set_current_location(first_tap_location);
  generator_->PressTouch();
  generator_->ReleaseTouch();
  gfx::Point second_tap_location(23, 24);
  generator_->set_current_location(second_tap_location);
  generator_->PressTouch();
  // Advance to the finger passing through, and then to the longpress timeout.
  AdvanceSimulatedTimePastTapDelay();
  simulated_clock_->Advance(gesture_detector_config_.longpress_timeout);
  generator_->ReleaseTouch();

  std::vector<ui::LocatedEvent*> captured_events = GetCapturedLocatedEvents();
  ASSERT_EQ(2U, captured_events.size());
  EXPECT_EQ(ui::ET_TOUCH_PRESSED, captured_events[0]->type());
  EXPECT_EQ(tap_location, captured_events[0]->location());
  base::TimeDelta pressed_time = captured_events[0]->time_stamp();
  EXPECT_EQ(ui::ET_TOUCH_RELEASED, captured_events[1]->type());
  EXPECT_EQ(tap_location, captured_events[1]->location());
  base::TimeDelta released_time = captured_events[1]->time_stamp();
  EXPECT_EQ(released_time - pressed_time,
            gesture_detector_config_.longpress_timeout);
}

// Single-tapping should send a touch press and release through to the location
// of the last successful touch exploration if the grace period has not
// elapsed.
TEST_F(TouchExplorationTest, SingleTap) {
  SwitchTouchExplorationMode(true);

  // Tap once to simulate a mouse moved event.
  gfx::Point initial_location(11, 12);
  generator_->set_current_location(initial_location);
  generator_->PressTouch();
  AdvanceSimulatedTimePastTapDelay();
  ClearCapturedEvents();

  // Move to another location for single tap
  gfx::Point tap_location(22, 23);
  generator_->MoveTouch(tap_location);
  generator_->ReleaseTouch();

  // Allow time to pass within the grace period of releasing before
  // tapping again.
  gfx::Point final_location(33, 34);
  generator_->set_current_location(final_location);
  simulated_clock_->Advance(base::TimeDelta::FromMilliseconds(250));
  generator_->PressTouch();
  generator_->ReleaseTouch();

  std::vector<ui::LocatedEvent*> captured_events = GetCapturedLocatedEvents();
  ASSERT_EQ(4U, captured_events.size());
  EXPECT_EQ(ui::ET_MOUSE_MOVED, captured_events[0]->type());
  EXPECT_EQ(ui::ET_MOUSE_MOVED, captured_events[1]->type());
  EXPECT_EQ(ui::ET_TOUCH_PRESSED, captured_events[2]->type());
  EXPECT_EQ(tap_location, captured_events[2]->location());
  EXPECT_EQ(ui::ET_TOUCH_RELEASED, captured_events[3]->type());
  EXPECT_EQ(tap_location, captured_events[3]->location());
}

// Double-tapping without coming from touch exploration (no previous touch
// exploration event) should not generate any events.
TEST_F(TouchExplorationTest, DoubleTapNoTouchExplore) {
  SwitchTouchExplorationMode(true);

  // Double-tap without any previous touch.
  // Touch exploration mode has not been entered, so there is no previous
  // touch exploration event. The double-tap should be discarded, and no events
  // should be generated at all.
  gfx::Point double_tap_location(33, 34);
  generator_->set_current_location(double_tap_location);
  generator_->PressTouch();
  generator_->ReleaseTouch();
  generator_->PressTouch();
  // Since the state stays in single_tap_released, we need to make sure the
  // tap timer doesn't fire and set the state to no fingers down (since there
  // is still a finger down).
  AdvanceSimulatedTimePastPotentialTapDelay();
  EXPECT_FALSE(IsInNoFingersDownState());
  generator_->ReleaseTouch();
  EXPECT_TRUE(IsInNoFingersDownState());

  std::vector<ui::LocatedEvent*> captured_events = GetCapturedLocatedEvents();
  ASSERT_EQ(0U, captured_events.size());
}

// Tapping and releasing with a second finger when in touch exploration mode
// should send a touch press and released to the location of the last
// successful touch exploration and return to touch explore.
TEST_F(TouchExplorationTest, SplitTap) {
  SwitchTouchExplorationMode(true);
  gfx::Point initial_touch_location(11, 12);
  gfx::Point second_touch_location(33, 34);

  // Tap and hold at one location, and get a mouse move event in touch explore.
  EnterTouchExplorationModeAtLocation(initial_touch_location);
  std::vector<ui::LocatedEvent*> events =
      GetCapturedLocatedEventsOfType(ui::ET_MOUSE_MOVED);
  ASSERT_EQ(1U, events.size());

  EXPECT_EQ(initial_touch_location, events[0]->location());
  EXPECT_TRUE(events[0]->flags() & ui::EF_IS_SYNTHESIZED);
  EXPECT_TRUE(events[0]->flags() & ui::EF_TOUCH_ACCESSIBILITY);
  ClearCapturedEvents();
  EXPECT_TRUE(IsInTouchToMouseMode());

  // Now tap and release at a different location. This should result in a
  // single touch and release at the location of the first (held) tap,
  // not at the location of the second tap and release.
  // After the release, there is still a finger in touch explore mode.
  ui::TouchEvent split_tap_press(
      ui::ET_TOUCH_PRESSED, second_touch_location, 1, Now());
  generator_->Dispatch(&split_tap_press);
  // To simulate the behavior of the real device, we manually disable
  // mouse events. To not rely on manually setting the state, this is also
  // tested in touch_exploration_controller_browsertest.
  cursor_client()->DisableMouseEvents();
  EXPECT_FALSE(cursor_client()->IsMouseEventsEnabled());
  EXPECT_FALSE(cursor_client()->IsCursorVisible());
  EXPECT_FALSE(IsInGestureInProgressState());
  ui::TouchEvent split_tap_release(
      ui::ET_TOUCH_RELEASED, second_touch_location, 1, Now());
  generator_->Dispatch(&split_tap_release);
  // Releasing the second finger should re-enable mouse events putting us
  // back into the touch exploration mode.
  EXPECT_TRUE(IsInTouchToMouseMode());
  EXPECT_FALSE(IsInNoFingersDownState());

  std::vector<ui::LocatedEvent*> captured_events = GetCapturedLocatedEvents();
  ASSERT_EQ(2U, captured_events.size());
  EXPECT_EQ(ui::ET_TOUCH_PRESSED, captured_events[0]->type());
  EXPECT_EQ(initial_touch_location, captured_events[0]->location());
  EXPECT_EQ(ui::ET_TOUCH_RELEASED, captured_events[1]->type());
  EXPECT_EQ(initial_touch_location, captured_events[1]->location());
  ClearCapturedEvents();

  ui::TouchEvent touch_explore_release(
      ui::ET_TOUCH_RELEASED, initial_touch_location, 0, Now());
  generator_->Dispatch(&touch_explore_release);
  AdvanceSimulatedTimePastTapDelay();
  EXPECT_TRUE(IsInNoFingersDownState());
}

// If split tap is started but the touch explore finger is released first,
// there should still be a touch press and release sent to the location of
// the last successful touch exploration.
// Both fingers should be released after the click goes through.
TEST_F(TouchExplorationTest, SplitTapRelease) {
  SwitchTouchExplorationMode(true);

  gfx::Point initial_touch_location(11, 12);
  gfx::Point second_touch_location(33, 34);

  // Tap and hold at one location, and get a mouse move event in touch explore.
  EnterTouchExplorationModeAtLocation(initial_touch_location);

  std::vector<ui::LocatedEvent*> events =
      GetCapturedLocatedEventsOfType(ui::ET_MOUSE_MOVED);
  ASSERT_EQ(1U, events.size());

  ClearCapturedEvents();

  // Now tap at a different location. Release at the first location,
  // then release at the second. This should result in a
  // single touch and release at the location of the first (held) tap,
  // not at the location of the second tap and release.
  ui::TouchEvent split_tap_press(
      ui::ET_TOUCH_PRESSED, second_touch_location, 1, Now());
  generator_->Dispatch(&split_tap_press);
  ui::TouchEvent touch_explore_release(
      ui::ET_TOUCH_RELEASED, initial_touch_location, 0, Now());
  generator_->Dispatch(&touch_explore_release);
  ui::TouchEvent split_tap_release(
      ui::ET_TOUCH_RELEASED, second_touch_location , 1, Now());
  generator_->Dispatch(&split_tap_release);
  EXPECT_TRUE(IsInNoFingersDownState());

  std::vector<ui::LocatedEvent*> captured_events = GetCapturedLocatedEvents();
  ASSERT_EQ(2U, captured_events.size());
  EXPECT_EQ(ui::ET_TOUCH_PRESSED, captured_events[0]->type());
  EXPECT_EQ(initial_touch_location, captured_events[0]->location());
  EXPECT_EQ(ui::ET_TOUCH_RELEASED, captured_events[1]->type());
  EXPECT_EQ(initial_touch_location, captured_events[1]->location());
}

// When in touch exploration mode, making a long press with a second finger
// should send a touch press and released to the location of the last
// successful touch exploration. There should be a delay between the
// touch and release events (right click).
TEST_F(TouchExplorationTest, SplitTapLongPress) {
  SwitchTouchExplorationMode(true);
  gfx::Point initial_touch_location(11, 12);
  gfx::Point second_touch_location(33, 34);

  // Tap and hold at one location, and get a mouse move event in touch explore.
  EnterTouchExplorationModeAtLocation(initial_touch_location);
  std::vector<ui::LocatedEvent*> events =
      GetCapturedLocatedEventsOfType(ui::ET_MOUSE_MOVED);
  ASSERT_EQ(1U, events.size());

  ClearCapturedEvents();

  // Now tap, hold, and release at a different location. This should result
  // in a single touch and release (long press) at the location of the first
  // (held) tap, not at the location of the second tap and release.
  ui::TouchEvent split_tap_press(
      ui::ET_TOUCH_PRESSED, second_touch_location, 1, Now());
  generator_->Dispatch(&split_tap_press);
  // To simulate the behavior of the real device, we manually disable
  // mouse events, like in the SplitTap test.
  cursor_client()->DisableMouseEvents();
  EXPECT_FALSE(cursor_client()->IsMouseEventsEnabled());
  EXPECT_FALSE(cursor_client()->IsCursorVisible());
  EXPECT_FALSE(IsInGestureInProgressState());
  simulated_clock_->Advance(gesture_detector_config_.longpress_timeout);
  // After the release, there is still a finger in touch exploration, and
  // mouse events should be enabled again.
  ui::TouchEvent split_tap_release(
      ui::ET_TOUCH_RELEASED, second_touch_location, 1, Now());
  generator_->Dispatch(&split_tap_release);
  EXPECT_FALSE(IsInNoFingersDownState());
  EXPECT_TRUE(IsInTouchToMouseMode());

  std::vector<ui::LocatedEvent*> captured_events = GetCapturedLocatedEvents();
  ASSERT_EQ(2U, captured_events.size());
  EXPECT_EQ(ui::ET_TOUCH_PRESSED, captured_events[0]->type());
  EXPECT_EQ(initial_touch_location, captured_events[0]->location());
  base::TimeDelta pressed_time = captured_events[0]->time_stamp();
  EXPECT_EQ(ui::ET_TOUCH_RELEASED, captured_events[1]->type());
  EXPECT_EQ(initial_touch_location, captured_events[1]->location());
  base::TimeDelta released_time = captured_events[1]->time_stamp();
  EXPECT_EQ(gesture_detector_config_.longpress_timeout,
            released_time - pressed_time);
}

// If split tap is started but the touch explore finger is released first,
// there should still be a touch press and release sent to the location of
// the last successful touch exploration. If the remaining finger is held
// as a longpress, there should be a delay between the sent touch and release
// events (right click).All fingers should be released after the click
// goes through.
TEST_F(TouchExplorationTest, SplitTapReleaseLongPress) {
  SwitchTouchExplorationMode(true);
  gfx::Point initial_touch_location(11, 12);
  gfx::Point second_touch_location(33, 34);

  // Tap and hold at one location, and get a mouse move event in touch explore.
  EnterTouchExplorationModeAtLocation(initial_touch_location);
  std::vector<ui::LocatedEvent*> events =
      GetCapturedLocatedEventsOfType(ui::ET_MOUSE_MOVED);
  ASSERT_EQ(1U, events.size());
  ClearCapturedEvents();

  // Now tap at a different location. Release at the first location,
  // then release at the second. This should result in a
  // single touch and release at the location of the first (held) tap,
  // not at the location of the second tap and release.
  // After the release, TouchToMouseMode should still be on.
  ui::TouchEvent split_tap_press(
      ui::ET_TOUCH_PRESSED, second_touch_location, 1, Now());
  generator_->Dispatch(&split_tap_press);
  ui::TouchEvent touch_explore_release(
      ui::ET_TOUCH_RELEASED, initial_touch_location, 0, Now());
  generator_->Dispatch(&touch_explore_release);
  simulated_clock_->Advance(gesture_detector_config_.longpress_timeout);
  ui::TouchEvent split_tap_release(
      ui::ET_TOUCH_RELEASED, second_touch_location, 1, Now());
  generator_->Dispatch(&split_tap_release);
  EXPECT_TRUE(IsInTouchToMouseMode());

  std::vector<ui::LocatedEvent*> captured_events = GetCapturedLocatedEvents();
  ASSERT_EQ(2U, captured_events.size());
  EXPECT_EQ(ui::ET_TOUCH_PRESSED, captured_events[0]->type());
  EXPECT_EQ(initial_touch_location, captured_events[0]->location());
  base::TimeDelta pressed_time = captured_events[0]->time_stamp();
  EXPECT_EQ(ui::ET_TOUCH_RELEASED, captured_events[1]->type());
  EXPECT_EQ(initial_touch_location, captured_events[1]->location());
  base::TimeDelta released_time = captured_events[1]->time_stamp();
  EXPECT_EQ(gesture_detector_config_.longpress_timeout,
            released_time - pressed_time);
 }

TEST_F(TouchExplorationTest, SplitTapMultiFinger) {
  SwitchTouchExplorationMode(true);
  gfx::Point initial_touch_location(11, 12);
  gfx::Point second_touch_location(33, 34);
  gfx::Point third_touch_location(16, 17);

  // Tap and hold at one location, and get a mouse move event in touch explore.
  EnterTouchExplorationModeAtLocation(initial_touch_location);

  std::vector<ui::LocatedEvent*> events =
      GetCapturedLocatedEventsOfType(ui::ET_MOUSE_MOVED);
  ASSERT_EQ(1U, events.size());

  EXPECT_EQ(initial_touch_location, events[0]->location());
  EXPECT_TRUE(events[0]->flags() & ui::EF_IS_SYNTHESIZED);
  EXPECT_TRUE(events[0]->flags() & ui::EF_TOUCH_ACCESSIBILITY);
  ClearCapturedEvents();

  // Now tap at a different location and hold for long press.
  ui::TouchEvent split_tap_press(
      ui::ET_TOUCH_PRESSED, second_touch_location, 1, Now());
  generator_->Dispatch(&split_tap_press);
  simulated_clock_->Advance(gesture_detector_config_.longpress_timeout);

  // Placing a third finger on the screen should cancel the initial press and
  // enter the wait state.
  ui::TouchEvent third_press(
      ui::ET_TOUCH_PRESSED, third_touch_location, 2, Now());
  generator_->Dispatch(&third_press);

  // When all three fingers are released, the only events captured should be a
  // press and touch cancel. All fingers should then be up.
  ui::TouchEvent touch_explore_release(
      ui::ET_TOUCH_RELEASED, initial_touch_location, 0, Now());
  generator_->Dispatch(&touch_explore_release);
  ui::TouchEvent split_tap_release(
      ui::ET_TOUCH_RELEASED, second_touch_location, 1, Now());
  generator_->Dispatch(&split_tap_release);
  ui::TouchEvent third_tap_release(
      ui::ET_TOUCH_RELEASED, third_touch_location, 2, Now());
  generator_->Dispatch(&third_tap_release);

  std::vector<ui::LocatedEvent*> captured_events = GetCapturedLocatedEvents();
  ASSERT_EQ(2U, captured_events.size());
  EXPECT_EQ(ui::ET_TOUCH_PRESSED, captured_events[0]->type());
  EXPECT_EQ(initial_touch_location, captured_events[0]->location());
  EXPECT_EQ(ui::ET_TOUCH_CANCELLED, captured_events[1]->type());
  EXPECT_EQ(initial_touch_location, captured_events[1]->location());
  EXPECT_TRUE(IsInNoFingersDownState());
}


TEST_F(TouchExplorationTest, SplitTapLeaveSlop) {
  SwitchTouchExplorationMode(true);
  gfx::Point first_touch_location(11, 12);
  gfx::Point second_touch_location(33, 34);
  gfx::Point first_move_location(
      first_touch_location.x() + gesture_detector_config_.touch_slop * 3 + 1,
      first_touch_location.y());
  gfx::Point second_move_location(
      second_touch_location.x() + gesture_detector_config_.touch_slop * 3 + 1,
      second_touch_location.y());

  // Tap and hold at one location, and get a mouse move event in touch explore.
  EnterTouchExplorationModeAtLocation(first_touch_location);
  ClearCapturedEvents();

  // Now tap at a different location for split tap.
  ui::TouchEvent split_tap_press(
      ui::ET_TOUCH_PRESSED, second_touch_location, 1, Now());
  generator_->Dispatch(&split_tap_press);

  // Move the first finger out of slop and release both fingers. The split
  // tap should have been cancelled, so a touch press and touch cancel event
  // should go through at the last touch exploration location (the first press).
  ui::TouchEvent first_touch_move(
      ui::ET_TOUCH_MOVED, first_move_location, 0, Now());
  generator_->Dispatch(&first_touch_move);
  ui::TouchEvent first_touch_release(
      ui::ET_TOUCH_RELEASED, first_move_location, 0, Now());
  generator_->Dispatch(&first_touch_release);
  ui::TouchEvent second_touch_release(
      ui::ET_TOUCH_RELEASED, second_touch_location, 1, Now());
  generator_->Dispatch(&second_touch_release);

  std::vector<ui::LocatedEvent*> captured_events = GetCapturedLocatedEvents();
  ASSERT_EQ(2U, captured_events.size());
  EXPECT_EQ(ui::ET_TOUCH_PRESSED, captured_events[0]->type());
  EXPECT_EQ(first_touch_location, captured_events[0]->location());
  EXPECT_EQ(ui::ET_TOUCH_CANCELLED, captured_events[1]->type());
  EXPECT_EQ(first_touch_location, captured_events[1]->location());
  EXPECT_TRUE(IsInNoFingersDownState());

  // Now do the same, but moving the split tap finger out of slop
  EnterTouchExplorationModeAtLocation(first_touch_location);
  ClearCapturedEvents();
  ui::TouchEvent split_tap_press2(
      ui::ET_TOUCH_PRESSED, second_touch_location, 1, Now());
  generator_->Dispatch(&split_tap_press2);

  // Move the second finger out of slop and release both fingers. The split
  // tap should have been cancelled, so a touch press and touch cancel event
  // should go through at the last touch exploration location (the first press).
  ui::TouchEvent second_touch_move2(
      ui::ET_TOUCH_MOVED, second_move_location, 1, Now());
  generator_->Dispatch(&second_touch_move2);
  ui::TouchEvent first_touch_release2(
      ui::ET_TOUCH_RELEASED, first_touch_location, 0, Now());
  generator_->Dispatch(&first_touch_release2);
  ui::TouchEvent second_touch_release2(
      ui::ET_TOUCH_RELEASED, second_move_location, 1, Now());
  generator_->Dispatch(&second_touch_release2);

  captured_events = GetCapturedLocatedEvents();
  ASSERT_EQ(2U, captured_events.size());
  EXPECT_EQ(ui::ET_TOUCH_PRESSED, captured_events[0]->type());
  EXPECT_EQ(first_touch_location, captured_events[0]->location());
  EXPECT_EQ(ui::ET_TOUCH_CANCELLED, captured_events[1]->type());
  EXPECT_EQ(first_touch_location, captured_events[1]->location());
  EXPECT_TRUE(IsInNoFingersDownState());
}

// Finger must have moved more than slop, faster than the minimum swipe
// velocity, and before the tap timer fires in order to enter
// GestureInProgress state. Otherwise, if the tap timer fires before the a
// gesture is completed, enter touch exploration.
TEST_F(TouchExplorationTest, EnterGestureInProgressState) {
  SwitchTouchExplorationMode(true);
  EXPECT_FALSE(IsInTouchToMouseMode());
  EXPECT_FALSE(IsInGestureInProgressState());

  float distance = gesture_detector_config_.touch_slop + 1;
  ui::TouchEvent first_press(ui::ET_TOUCH_PRESSED, gfx::Point(0, 1), 0, Now());
  gfx::Point second_location(distance / 2, 1);
  gfx::Point third_location(distance, 1);
  gfx::Point touch_exploration_location(20, 21);

  generator_->Dispatch(&first_press);
  simulated_clock_->Advance(base::TimeDelta::FromMilliseconds(10));
  // Since we are not out of the touch slop yet, we should not be in gesture in
  // progress.
  generator_->MoveTouch(second_location);
  EXPECT_FALSE(IsInTouchToMouseMode());
  EXPECT_FALSE(IsInGestureInProgressState());
  simulated_clock_->Advance(base::TimeDelta::FromMilliseconds(10));

  // Once we are out of slop, we should be in GestureInProgress.
  generator_->MoveTouch(third_location);
  EXPECT_TRUE(IsInGestureInProgressState());
  EXPECT_FALSE(IsInTouchToMouseMode());
  const ScopedVector<ui::Event>& captured_events = GetCapturedEvents();
  ASSERT_EQ(0U, captured_events.size());

  // Exit out of gesture mode once grace period is over and enter touch
  // exploration. There should be a move when entering touch exploration and
  // also for the touch move.
  AdvanceSimulatedTimePastTapDelay();
  generator_->MoveTouch(touch_exploration_location);
  ASSERT_EQ(2U, captured_events.size());
  EXPECT_EQ(ui::ET_MOUSE_MOVED, captured_events[0]->type());
  EXPECT_EQ(ui::ET_MOUSE_MOVED, captured_events[1]->type());
  EXPECT_TRUE(IsInTouchToMouseMode());
  EXPECT_FALSE(IsInGestureInProgressState());
}

// A swipe+direction gesture should trigger a Shift+Search+Direction
// keyboard event.
TEST_F(TouchExplorationTest, GestureSwipe) {
  SwitchTouchExplorationMode(true);
  std::vector<ui::KeyboardCode> directions;
  directions.push_back(ui::VKEY_RIGHT);
  directions.push_back(ui::VKEY_LEFT);
  directions.push_back(ui::VKEY_UP);
  directions.push_back(ui::VKEY_DOWN);

  // This value was taken from gesture_recognizer_unittest.cc in a swipe
  // detector test, since it seems to be about the right amount to get a swipe.
  const int kSteps = 15;

  // There are gestures supported with up to four fingers.
  for (int num_fingers = 1; num_fingers <= 4; num_fingers++) {
    std::vector<gfx::Point> start_points;
    for (int j = 0; j < num_fingers; j++) {
      start_points.push_back(gfx::Point(j * 10 + 100, j * 10 + 200));
    }
    gfx::Point* start_points_array = &start_points[0];
    const float distance = gesture_detector_config_.touch_slop + 1;
    // Iterate through each swipe direction for this number of fingers.
    for (std::vector<ui::KeyboardCode>::const_iterator it = directions.begin();
         it != directions.end();
         ++it) {
      int move_x = 0;
      int move_y = 0;
      ui::KeyboardCode direction = *it;
      switch (direction) {
        case ui::VKEY_RIGHT:
          move_x = distance;
          break;
        case ui::VKEY_LEFT:
          move_x = 0 - distance;
          break;
        case ui::VKEY_UP:
          move_y = 0 - distance;
          break;
        case ui::VKEY_DOWN:
          move_y = distance;
          break;
        default:
          return;
      }

      // A swipe is made when a fling starts
      float delta_time =
          distance / gesture_detector_config_.maximum_fling_velocity;
      // delta_time is in seconds, so we convert to ms.
      int delta_time_ms = floor(delta_time * 1000);
      generator_->GestureMultiFingerScroll(num_fingers,
                                           start_points_array,
                                           delta_time_ms,
                                           kSteps,
                                           move_x * 2,
                                           move_y * 2);

      // The swipe registered and sent the appropriate key events.
      const ScopedVector<ui::Event>& captured_events = GetCapturedEvents();
      if (num_fingers == 1)
        AssertDirectionalNavigationEvents(captured_events, direction);
      else {
        // Most of the time this is 2 right now, but two of the two finger
        // swipes are mapped to chromevox commands which dispatch 6 key events,
        // and these will probably be remapped a lot as we're developing.
        ASSERT_GE(captured_events.size(), 2U);
        std::vector<ui::Event>::size_type i;
        for (i = 0; i != captured_events.size(); i++) {
          EXPECT_TRUE(captured_events[i]->IsKeyEvent());
        }
      }
      EXPECT_TRUE(IsInNoFingersDownState());
      EXPECT_FALSE(IsInTouchToMouseMode());
      EXPECT_FALSE(IsInGestureInProgressState());
      ClearCapturedEvents();
    }
  }
}

// Since there are so many permutations, this test is fairly slow. Therefore, it
// is disabled and will be turned on to check during development.

TEST_F(TouchExplorationTest, DISABLED_AllFingerPermutations) {
  SwitchTouchExplorationMode(true);
  SuppressVLOGs(true);
  // We will test all permutations of events from three different fingers
  // to ensure that we return to NO_FINGERS_DOWN when fingers have been
  // released.
  ScopedVector<ui::TouchEvent> all_events;
  for (int touch_id = 0; touch_id < 3; touch_id++){
    int x = 10*touch_id + 1;
    int y = 10*touch_id + 2;
    all_events.push_back(new TouchEvent(
        ui::ET_TOUCH_PRESSED, gfx::Point(x++, y++), touch_id, Now()));
    all_events.push_back(new TouchEvent(
        ui::ET_TOUCH_MOVED, gfx::Point(x++, y++), touch_id, Now()));
    all_events.push_back(new TouchEvent(
        ui::ET_TOUCH_RELEASED, gfx::Point(x, y), touch_id, Now()));
  }

  // I'm going to explain this algorithm, and use an example in parentheses.
  // The example will be all permutations of a b c d.
  // There are four letters and 4! = 24 permutations.
  const int num_events = all_events.size();
  const int num_permutations = Factorial(num_events);

  for (int p = 0; p < num_permutations; p++) {
    std::vector<ui::TouchEvent*> queued_events = all_events.get();
    std::vector<bool> fingers_pressed(3, false);

    int current_num_permutations = num_permutations;
    for (int events_left = num_events; events_left > 0; events_left--) {
      // |p| indexes to each permutation when there are num_permutations
      // permutations. (e.g. 0 is abcd, 1 is abdc, 2 is acbd, 3 is acdb...)
      // But how do we find the index for the current number of permutations?
      // To find the permutation within the part of the sequence we're
      // currently looking at, we need a number between 0 and
      // |current_num_permutations| - 1.
      // (e.g. if we already chose the first letter, there are 3! = 6
      // options left, so we do p % 6. So |current_permutation| would go
      // from 0 to 5 and then reset to 0 again, for all combinations of
      // whichever three letters are remaining, as we loop through the
      // permutations)
      int current_permutation = p % current_num_permutations;

      // Since this is is the total number of permutations starting with
      // this event and including future events, there could be multiple
      // values of current_permutation that will generate the same event
      // in this iteration.
      // (e.g. If we chose 'a' but have b c d to choose from, we choose b when
      // |current_permutation| = 0, 1 and c when |current_permutation| = 2, 3.
      // Note that each letter gets two numbers, which is the next
      // current_num_permutations, 2! for the two letters left.)

      // Branching out from the first event, there are num_permutations
      // permutations, and each value of |p| is associated with one of these
      // permutations. However, once the first event is chosen, there
      // are now |num_events| - 1 events left, so the number of permutations
      // for the rest of the events changes, and will always be equal to
      // the factorial of the events_left.
      // (e.g. There are 3! = 6 permutations that start with 'a', so if we
      // start with 'a' there will be 6 ways to then choose from b c d.)
      // So we now set-up for the next iteration by setting
      // current_num_permutations to the factorial of the next number of
      // events left.
      current_num_permutations /= events_left;

      // To figure out what current event we want to choose, we integer
      // divide the current permutation by the next current_num_permutations.
      // (e.g. If there are 4 letters a b c d and 24 permutations, we divide
      // by 24/4 = 6. Values 0 to 5 when divided by 6 equals 0, so the first
      // 6 permutations start with 'a', and the last 6 will start with 'd'.
      // Note that there are 6 that start with 'a' because there are 6
      // permutations for the next three letters that follow 'a'.)
      int index = current_permutation / current_num_permutations;

      ui::TouchEvent* next_dispatch = queued_events[index];
      ASSERT_TRUE(next_dispatch != NULL);

      // |next_dispatch| has to be put in this container so that its time
      // stamp can be changed to this point in the test, when it is being
      // dispatched..
      EventTestApi test_dispatch(next_dispatch);
      test_dispatch.set_time_stamp(Now());
      generator_->Dispatch(next_dispatch);
      queued_events.erase(queued_events.begin() + index);

      // Keep track of what fingers have been pressed, to release
      // only those fingers at the end, so the check for being in
      // no fingers down can be accurate.
      if (next_dispatch->type() == ET_TOUCH_PRESSED) {
        fingers_pressed[next_dispatch->touch_id()] = true;
      } else if (next_dispatch->type() == ET_TOUCH_RELEASED) {
        fingers_pressed[next_dispatch->touch_id()] = false;
      }
    }
    ASSERT_EQ(queued_events.size(), 0u);

    // Release fingers recorded as pressed.
    for(int j = 0; j < int(fingers_pressed.size()); j++){
      if (fingers_pressed[j] == true) {
        generator_->ReleaseTouchId(j);
        fingers_pressed[j] = false;
      }
    }
    AdvanceSimulatedTimePastPotentialTapDelay();
    EXPECT_TRUE(IsInNoFingersDownState());
    ClearCapturedEvents();
  }
}

// With the simple swipe gestures, if additional fingers are added and the tap
// timer times out, then the state should change to the wait for one finger
// state.
TEST_F(TouchExplorationTest, GestureAddedFinger) {
  SwitchTouchExplorationMode(true);
  EXPECT_FALSE(IsInTouchToMouseMode());
  EXPECT_FALSE(IsInGestureInProgressState());

  float distance = gesture_detector_config_.touch_slop + 1;
  ui::TouchEvent first_press(
      ui::ET_TOUCH_PRESSED, gfx::Point(100, 200), 0, Now());
  generator_->Dispatch(&first_press);
  simulated_clock_->Advance(base::TimeDelta::FromMilliseconds(10));
  gfx::Point second_location(100 + distance, 200);
  generator_->MoveTouch(second_location);
  EXPECT_TRUE(IsInGestureInProgressState());
  EXPECT_FALSE(IsInTouchToMouseMode());
  const ScopedVector<ui::Event>& captured_events = GetCapturedEvents();
  ASSERT_EQ(0U, captured_events.size());

  // Generate a second press, but time out past the gesture period so that
  // gestures are prevented from continuing to go through.
  ui::TouchEvent second_press(
      ui::ET_TOUCH_PRESSED, gfx::Point(20, 21), 1, Now());
  generator_->Dispatch(&second_press);
  AdvanceSimulatedTimePastTapDelay();
  EXPECT_FALSE(IsInGestureInProgressState());
  EXPECT_FALSE(IsInTouchToMouseMode());
  ASSERT_EQ(0U, captured_events.size());
}

TEST_F(TouchExplorationTest, EnterSlideGestureState) {
  SwitchTouchExplorationMode(true);
  EXPECT_FALSE(IsInTouchToMouseMode());
  EXPECT_FALSE(IsInGestureInProgressState());

  int window_right = BoundsOfRootWindowInDIP().right();
  float distance = gesture_detector_config_.touch_slop + 1;
  ui::TouchEvent first_press(
      ui::ET_TOUCH_PRESSED, gfx::Point(window_right, 1), 0, Now());
  gfx::Point second_location(window_right, 1 + distance / 2);
  gfx::Point third_location(window_right, 1 + distance);
  gfx::Point fourth_location(window_right, 35);

  generator_->Dispatch(&first_press);
  simulated_clock_->Advance(base::TimeDelta::FromMilliseconds(10));

  // Since we haven't moved past slop yet, we should not be in slide gesture.
  generator_->MoveTouch(second_location);
  EXPECT_FALSE(IsInTouchToMouseMode());
  EXPECT_FALSE(IsInGestureInProgressState());
  EXPECT_FALSE(IsInSlideGestureState());
  simulated_clock_->Advance(base::TimeDelta::FromMilliseconds(10));

  // Once we are out of slop, we should be in slide gesture since we are along
  // the edge of the screen.
  generator_->MoveTouch(third_location);
  EXPECT_FALSE(IsInGestureInProgressState());
  EXPECT_TRUE(IsInSlideGestureState());
  EXPECT_FALSE(IsInTouchToMouseMode());

  // Now that we are in slide gesture, we can adjust the volume.
  generator_->MoveTouch(fourth_location);
  const ScopedVector<ui::Event>& captured_events = GetCapturedEvents();
  ASSERT_EQ(0U, captured_events.size());

  // Since we are at the right edge of the screen, but the sound timer has not
  // elapsed, there should have been a sound that fired and a volume
  // change.
  size_t num_adjust_sounds = delegate_.NumAdjustSounds();
  ASSERT_EQ(1U, num_adjust_sounds);
  ASSERT_EQ(1U, delegate_.VolumeChanges().size());

  // Exit out of slide gesture once touch is lifted, but not before even if the
  // grace period is over.
  AdvanceSimulatedTimePastPotentialTapDelay();
  ASSERT_EQ(0U, captured_events.size());
  EXPECT_FALSE(IsInTouchToMouseMode());
  EXPECT_FALSE(IsInGestureInProgressState());
  EXPECT_TRUE(IsInSlideGestureState());

  generator_->ReleaseTouch();
  ASSERT_EQ(0U, captured_events.size());
  EXPECT_FALSE(IsInTouchToMouseMode());
  EXPECT_FALSE(IsInGestureInProgressState());
  EXPECT_FALSE(IsInSlideGestureState());
}

// If a press + move occurred outside the boundaries, but within the slop
// boundaries and then moved into the boundaries of an edge, there still should
// not be a slide gesture.
TEST_F(TouchExplorationTest, AvoidEnteringSlideGesture) {
  SwitchTouchExplorationMode(true);

  gfx::Rect window = BoundsOfRootWindowInDIP();
  float distance = gesture_detector_config_.touch_slop + 1;
  ui::TouchEvent first_press(
      ui::ET_TOUCH_PRESSED,
      gfx::Point(window.right() - GetSlopDistanceFromEdge(), 1),
      0,
      Now());
  gfx::Point out_of_slop(window.right() - GetSlopDistanceFromEdge() + distance,
                         1);
  gfx::Point into_boundaries(window.right() - GetMaxDistanceFromEdge() / 2, 1);

  generator_->Dispatch(&first_press);
  simulated_clock_->Advance(base::TimeDelta::FromMilliseconds(10));

  generator_->MoveTouch(out_of_slop);
  EXPECT_FALSE(IsInTouchToMouseMode());
  EXPECT_TRUE(IsInGestureInProgressState());
  EXPECT_FALSE(IsInSlideGestureState());
  simulated_clock_->Advance(base::TimeDelta::FromMilliseconds(10));

  // Since we did not start moving while in the boundaries, we should not be in
  // slide gestures.
  generator_->MoveTouch(into_boundaries);
  EXPECT_TRUE(IsInGestureInProgressState());
  EXPECT_FALSE(IsInSlideGestureState());
  EXPECT_FALSE(IsInTouchToMouseMode());
  const ScopedVector<ui::Event>& captured_events = GetCapturedEvents();
  ASSERT_EQ(0U, captured_events.size());

  generator_->ReleaseTouch();
}

// If the slide gesture begins within the boundaries and then moves
// SlopDistanceFromEdge there should still be a sound change. If the finger
// moves into the center screen, there should no longer be a sound change but it
// should still be in slide gesture. If the finger moves back into the edges
// without lifting, it should start changing sound again.
TEST_F(TouchExplorationTest, TestingBoundaries) {
  SwitchTouchExplorationMode(true);

  gfx::Rect window = BoundsOfRootWindowInDIP();
  gfx::Point initial_press(window.right() - GetMaxDistanceFromEdge() / 2, 1);

  gfx::Point center_screen(window.right() / 2, window.bottom() / 2);

  ui::TouchEvent first_press(ui::ET_TOUCH_PRESSED, initial_press, 0, Now());
  generator_->Dispatch(&first_press);
  simulated_clock_->Advance(base::TimeDelta::FromMilliseconds(10));
  EXPECT_FALSE(IsInGestureInProgressState());
  EXPECT_FALSE(IsInSlideGestureState());
  EXPECT_FALSE(IsInTouchToMouseMode());

  // Move past the touch slop to begin slide gestures.
  // + slop + 1 to actually leave slop.
  gfx::Point touch_move(
      initial_press.x(),
      initial_press.y() + gesture_detector_config_.touch_slop + 1);
  generator_->MoveTouch(touch_move);
  EXPECT_FALSE(IsInGestureInProgressState());
  EXPECT_TRUE(IsInSlideGestureState());
  EXPECT_FALSE(IsInTouchToMouseMode());
  simulated_clock_->Advance(base::TimeDelta::FromMilliseconds(10));

  // Move the touch into slop boundaries. It should still be in slide gestures
  // and adjust the volume.
  gfx::Point into_slop_boundaries(
      window.right() - GetSlopDistanceFromEdge() / 2, 1);
  generator_->MoveTouch(into_slop_boundaries);
  EXPECT_FALSE(IsInGestureInProgressState());
  EXPECT_TRUE(IsInSlideGestureState());
  EXPECT_FALSE(IsInTouchToMouseMode());

  // The sound is rate limiting so it only activates every 150ms.
  simulated_clock_->Advance(base::TimeDelta::FromMilliseconds(200));

  size_t num_adjust_sounds = delegate_.NumAdjustSounds();
  ASSERT_EQ(1U, num_adjust_sounds);
  ASSERT_EQ(1U, delegate_.VolumeChanges().size());

  // Move the touch into the center of the window. It should still be in slide
  // gestures, but there should not be anymore volume adjustments.
  generator_->MoveTouch(center_screen);
  EXPECT_FALSE(IsInGestureInProgressState());
  EXPECT_TRUE(IsInSlideGestureState());
  EXPECT_FALSE(IsInTouchToMouseMode());

  simulated_clock_->Advance(base::TimeDelta::FromMilliseconds(200));
  num_adjust_sounds = delegate_.NumAdjustSounds();
  ASSERT_EQ(1U, num_adjust_sounds);
  ASSERT_EQ(1U, delegate_.VolumeChanges().size());

  // Move the touch back into slop edge distance and volume should be changing
  // again, one volume change for each new move.
  generator_->MoveTouch(into_slop_boundaries);
  EXPECT_FALSE(IsInGestureInProgressState());
  EXPECT_TRUE(IsInSlideGestureState());
  EXPECT_FALSE(IsInTouchToMouseMode());

  generator_->MoveTouch(
      gfx::Point(into_slop_boundaries.x() + gesture_detector_config_.touch_slop,
                 into_slop_boundaries.y()));
  simulated_clock_->Advance(base::TimeDelta::FromMilliseconds(200));

  num_adjust_sounds = delegate_.NumAdjustSounds();
  ASSERT_EQ(2U, num_adjust_sounds);
  ASSERT_EQ(3U, delegate_.VolumeChanges().size());

  const ScopedVector<ui::Event>& captured_events = GetCapturedEvents();
  ASSERT_EQ(0U, captured_events.size());

  generator_->ReleaseTouch();
}

// Even if the gesture starts within bounds, if it has not moved past slop
// within the grace period, it should go to touch exploration.
TEST_F(TouchExplorationTest, InBoundariesTouchExploration) {
  SwitchTouchExplorationMode(true);

  gfx::Rect window = BoundsOfRootWindowInDIP();
  gfx::Point initial_press(window.right() - GetMaxDistanceFromEdge() / 2, 1);
  ui::TouchEvent first_press(
      ui::ET_TOUCH_PRESSED,
      initial_press,
      0,
      Now());
  generator_->Dispatch(&first_press);
  EXPECT_FALSE(IsInGestureInProgressState());
  EXPECT_FALSE(IsInSlideGestureState());
  EXPECT_FALSE(IsInTouchToMouseMode());

  AdvanceSimulatedTimePastTapDelay();
  EXPECT_FALSE(IsInGestureInProgressState());
  EXPECT_FALSE(IsInSlideGestureState());
  EXPECT_TRUE(IsInTouchToMouseMode());
}

// If two fingers tap the screen at the same time and release before the tap
// timer runs out, a control key event should be sent to silence chromevox.
TEST_F(TouchExplorationTest, TwoFingerTap) {
  SwitchTouchExplorationMode(true);

  generator_->set_current_location(gfx::Point(101, 102));
  generator_->PressTouchId(1);
  EXPECT_FALSE(IsInTwoFingerTapState());

  generator_->PressTouchId(2);
  EXPECT_TRUE(IsInTwoFingerTapState());

  const ScopedVector<ui::Event>& captured_events = GetCapturedEvents();
  ASSERT_EQ(0U, captured_events.size());

  generator_->ReleaseTouchId(1);
  EXPECT_TRUE(IsInTwoFingerTapState());
  generator_->ReleaseTouchId(2);

  // Two key events should have been sent to silence the feedback.
  EXPECT_EQ(2U, captured_events.size());
}

// If the fingers are not released before the tap timer runs out, a control
// keyevent is not sent and the state will no longer be in two finger tap.
TEST_F(TouchExplorationTest, TwoFingerTapAndHold) {
  SwitchTouchExplorationMode(true);

  generator_->PressTouchId(1);
  EXPECT_FALSE(IsInTwoFingerTapState());

  generator_->PressTouchId(2);
  EXPECT_TRUE(IsInTwoFingerTapState());

  const ScopedVector<ui::Event>& captured_events = GetCapturedEvents();
  ASSERT_EQ(0U, captured_events.size());

  AdvanceSimulatedTimePastTapDelay();
  // Since the tap delay has elapsed, it should no longer be in two finger tap.
  EXPECT_FALSE(IsInTwoFingerTapState());
}

// The next two tests set up two finger swipes to happen. If one of the fingers
// moves out of slop before the tap timer fires, a two finger tap is not made.
// In this first test, the first finger placed will move out of slop.
TEST_F(TouchExplorationTest, TwoFingerTapAndMoveFirstFinger) {
  SwitchTouchExplorationMode(true);

  // Once one of the fingers leaves slop, it should no longer be in two finger
  // tap.
  ui::TouchEvent first_press_id_1(
      ui::ET_TOUCH_PRESSED, gfx::Point(100, 200), 1, Now());
  ui::TouchEvent first_press_id_2(
      ui::ET_TOUCH_PRESSED, gfx::Point(110, 200), 2, Now());

  ui::TouchEvent slop_move_id_1(
      ui::ET_TOUCH_MOVED,
      gfx::Point(100 + gesture_detector_config_.touch_slop, 200),
      1,
      Now());
  ui::TouchEvent slop_move_id_2(
      ui::ET_TOUCH_MOVED,
      gfx::Point(110 + gesture_detector_config_.touch_slop, 200),
      2,
      Now());

  ui::TouchEvent out_slop_id_1(
      ui::ET_TOUCH_MOVED,
      gfx::Point(100 + gesture_detector_config_.touch_slop + 1, 200),
      1,
      Now());

  // Dispatch the inital presses.
  generator_->Dispatch(&first_press_id_1);
  EXPECT_FALSE(IsInTwoFingerTapState());
  generator_->Dispatch(&first_press_id_2);
  EXPECT_TRUE(IsInTwoFingerTapState());

  const ScopedVector<ui::Event>& captured_events = GetCapturedEvents();
  ASSERT_EQ(0U, captured_events.size());

  // The presses have not moved out of slop yet so it should still be in
  // TwoFingerTap.
  generator_->Dispatch(&slop_move_id_1);
  EXPECT_TRUE(IsInTwoFingerTapState());
  generator_->Dispatch(&slop_move_id_2);
  EXPECT_TRUE(IsInTwoFingerTapState());

  // Once one of the fingers moves out of slop, we are no longer in
  // TwoFingerTap.
  generator_->Dispatch(&out_slop_id_1);
  EXPECT_FALSE(IsInTwoFingerTapState());
}

// Similar test to the previous test except the second finger placed will be the
// one to move out of slop.
TEST_F(TouchExplorationTest, TwoFingerTapAndMoveSecondFinger) {
  SwitchTouchExplorationMode(true);

  // Once one of the fingers leaves slop, it should no longer be in two finger
  // tap.
  ui::TouchEvent first_press_id_1(
      ui::ET_TOUCH_PRESSED, gfx::Point(100, 200), 1, Now());
  ui::TouchEvent first_press_id_2(
      ui::ET_TOUCH_PRESSED, gfx::Point(110, 200), 2, Now());

  ui::TouchEvent out_slop_id_2(
      ui::ET_TOUCH_MOVED,
      gfx::Point(100 + gesture_detector_config_.touch_slop + 1, 200),
      1,
      Now());

  generator_->Dispatch(&first_press_id_1);
  EXPECT_FALSE(IsInTwoFingerTapState());

  generator_->Dispatch(&first_press_id_2);
  EXPECT_TRUE(IsInTwoFingerTapState());

  const ScopedVector<ui::Event>& captured_events = GetCapturedEvents();
  ASSERT_EQ(0U, captured_events.size());

  generator_->Dispatch(&out_slop_id_2);
  EXPECT_FALSE(IsInTwoFingerTapState());
}

// Corner passthrough should turn on if the user first holds down on either the
// right or left corner past a delay and then places a finger anywhere else on
// the screen.
TEST_F(TouchExplorationTest, ActivateLeftCornerPassthrough) {
  SwitchTouchExplorationMode(true);

  gfx::Rect window = BoundsOfRootWindowInDIP();
  gfx::Point left_corner(10, window.bottom() - GetMaxDistanceFromEdge() / 2);
  AssertCornerPassthroughWorking(left_corner);
}

TEST_F(TouchExplorationTest, ActivateRightCornerPassthrough) {
  SwitchTouchExplorationMode(true);

  gfx::Rect window = BoundsOfRootWindowInDIP();
  gfx::Point right_corner(window.right() - GetMaxDistanceFromEdge() / 2,
                          window.bottom() - GetMaxDistanceFromEdge() / 2);
  AssertCornerPassthroughWorking(right_corner);
}

// Earcons should play if the user slides off the screen or enters the screen
// from the edge.
TEST_F(TouchExplorationTest, EnterEarconPlays) {
  SwitchTouchExplorationMode(true);

  gfx::Rect window = BoundsOfRootWindowInDIP();

  gfx::Point upper_left_corner(0, 0);
  gfx::Point upper_right_corner(window.right(), 0);
  gfx::Point lower_left_corner(0, window.bottom());
  gfx::Point lower_right_corner(window.right(), window.bottom());
  gfx::Point left_edge(0, 30);
  gfx::Point right_edge(window.right(), 30);
  gfx::Point top_edge(30, 0);
  gfx::Point bottom_edge(30, window.bottom());

  std::vector<gfx::Point> locations;
  locations.push_back(upper_left_corner);
  locations.push_back(upper_right_corner);
  locations.push_back(lower_left_corner);
  locations.push_back(lower_right_corner);
  locations.push_back(left_edge);
  locations.push_back(right_edge);
  locations.push_back(top_edge);
  locations.push_back(bottom_edge);

  for (std::vector<gfx::Point>::const_iterator point = locations.begin();
       point != locations.end();
       ++point) {
    ui::TouchEvent touch_event(ui::ET_TOUCH_PRESSED, *point, 1, Now());

    generator_->Dispatch(&touch_event);
    ASSERT_EQ(1U, delegate_.NumEnterScreenSounds());
    generator_->ReleaseTouchId(1);
    delegate_.ResetCountersToZero();
  }
}

TEST_F(TouchExplorationTest, ExitEarconPlays) {
  SwitchTouchExplorationMode(true);

  // On the device, it cannot actually tell if the finger has left the screen or
  // not. If the finger has left the screen, it reads it as a release that
  // occurred very close to the edge of the screen even if the finger is still
  // technically touching the moniter. To simulate this, a release that occurs
  // close to the edge is dispatched.
  gfx::Point initial_press(100, 200);
  gfx::Rect window = BoundsOfRootWindowInDIP();

  gfx::Point upper_left_corner(0, 0);
  gfx::Point upper_right_corner(window.right(), 0);
  gfx::Point lower_left_corner(0, window.bottom());
  gfx::Point lower_right_corner(window.right(), window.bottom());
  gfx::Point left_edge(0, 30);
  gfx::Point right_edge(window.right(), 30);
  gfx::Point top_edge(30, 0);
  gfx::Point bottom_edge(30, window.bottom());

  std::vector<gfx::Point> locations;
  locations.push_back(upper_left_corner);
  locations.push_back(upper_right_corner);
  locations.push_back(lower_left_corner);
  locations.push_back(lower_right_corner);
  locations.push_back(left_edge);
  locations.push_back(right_edge);
  locations.push_back(top_edge);
  locations.push_back(bottom_edge);

  for (std::vector<gfx::Point>::const_iterator point = locations.begin();
       point != locations.end();
       ++point) {
    generator_->PressTouch();
    generator_->MoveTouch(initial_press);
    generator_->MoveTouch(*point);
    generator_->ReleaseTouch();
    ASSERT_EQ(1U, delegate_.NumExitScreenSounds());
    delegate_.ResetCountersToZero();
  }
}

}  // namespace ui
