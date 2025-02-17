// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/cast/logging/simple_event_subscriber.h"

#include <utility>

#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/test/simple_test_tick_clock.h"
#include "base/time/tick_clock.h"
#include "media/base/fake_single_thread_task_runner.h"
#include "media/cast/cast_environment.h"
#include "media/cast/logging/logging_defines.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace media {
namespace cast {

class SimpleEventSubscriberTest : public ::testing::Test {
 protected:
  SimpleEventSubscriberTest()
      : testing_clock_(new base::SimpleTestTickClock()),
        task_runner_(new FakeSingleThreadTaskRunner(testing_clock_)),
        cast_environment_(
            new CastEnvironment(scoped_ptr<base::TickClock>(testing_clock_),
                                task_runner_,
                                task_runner_,
                                task_runner_)) {
    cast_environment_->logger()->Subscribe(&event_subscriber_);
  }

  ~SimpleEventSubscriberTest() override {
    cast_environment_->logger()->Unsubscribe(&event_subscriber_);
  }

  base::SimpleTestTickClock* testing_clock_;  // Owned by CastEnvironment.
  scoped_refptr<FakeSingleThreadTaskRunner> task_runner_;
  scoped_refptr<CastEnvironment> cast_environment_;
  SimpleEventSubscriber event_subscriber_;
};

TEST_F(SimpleEventSubscriberTest, GetAndResetEvents) {
  // Log some frame events.
  scoped_ptr<FrameEvent> encode_event(new FrameEvent());
  encode_event->timestamp = testing_clock_->NowTicks();
  encode_event->type = FRAME_ENCODED;
  encode_event->media_type = AUDIO_EVENT;
  encode_event->rtp_timestamp = RtpTimeTicks().Expand(UINT32_C(100));
  encode_event->frame_id = 0u;
  encode_event->size = 1234;
  encode_event->key_frame = true;
  encode_event->target_bitrate = 128u;
  encode_event->encoder_cpu_utilization = 0.01;
  encode_event->idealized_bitrate_utilization = 0.02;
  cast_environment_->logger()->DispatchFrameEvent(std::move(encode_event));

  scoped_ptr<FrameEvent> playout_event(new FrameEvent());
  playout_event->timestamp = testing_clock_->NowTicks();
  playout_event->type = FRAME_PLAYOUT;
  playout_event->media_type = AUDIO_EVENT;
  playout_event->rtp_timestamp = RtpTimeTicks().Expand(UINT32_C(100));
  playout_event->frame_id = 0u;
  playout_event->delay_delta = base::TimeDelta::FromMilliseconds(100);
  cast_environment_->logger()->DispatchFrameEvent(std::move(playout_event));

  scoped_ptr<FrameEvent> decode_event(new FrameEvent());
  decode_event->timestamp = testing_clock_->NowTicks();
  decode_event->type = FRAME_DECODED;
  decode_event->media_type = AUDIO_EVENT;
  decode_event->rtp_timestamp = RtpTimeTicks().Expand(UINT32_C(200));
  decode_event->frame_id = 0u;
  cast_environment_->logger()->DispatchFrameEvent(std::move(decode_event));

  // Log some packet events.
  scoped_ptr<PacketEvent> receive_event(new PacketEvent());
  receive_event->timestamp = testing_clock_->NowTicks();
  receive_event->type = PACKET_RECEIVED;
  receive_event->media_type = AUDIO_EVENT;
  receive_event->rtp_timestamp = RtpTimeTicks().Expand(UINT32_C(200));
  receive_event->frame_id = 0u;
  receive_event->packet_id = 1u;
  receive_event->max_packet_id = 5u;
  receive_event->size = 100u;
  cast_environment_->logger()->DispatchPacketEvent(std::move(receive_event));

  receive_event.reset(new PacketEvent());
  receive_event->timestamp = testing_clock_->NowTicks();
  receive_event->type = PACKET_RECEIVED;
  receive_event->media_type = VIDEO_EVENT;
  receive_event->rtp_timestamp = RtpTimeTicks().Expand(UINT32_C(200));
  receive_event->frame_id = 0u;
  receive_event->packet_id = 1u;
  receive_event->max_packet_id = 10u;
  receive_event->size = 1024u;
  cast_environment_->logger()->DispatchPacketEvent(std::move(receive_event));

  std::vector<FrameEvent> frame_events;
  event_subscriber_.GetFrameEventsAndReset(&frame_events);
  EXPECT_EQ(3u, frame_events.size());

  std::vector<PacketEvent> packet_events;
  event_subscriber_.GetPacketEventsAndReset(&packet_events);
  EXPECT_EQ(2u, packet_events.size());

  // Calling this function again should result in empty vector because no events
  // were logged since last call.
  event_subscriber_.GetFrameEventsAndReset(&frame_events);
  event_subscriber_.GetPacketEventsAndReset(&packet_events);
  EXPECT_TRUE(frame_events.empty());
  EXPECT_TRUE(packet_events.empty());
}

}  // namespace cast
}  // namespace media
