// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_FILTERS_VIDEO_RENDERER_ALGORITHM_H_
#define MEDIA_FILTERS_VIDEO_RENDERER_ALGORITHM_H_

#include <stddef.h>
#include <stdint.h>

#include <deque>

#include "base/callback.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/time/time.h"
#include "media/base/media_export.h"
#include "media/base/moving_average.h"
#include "media/base/video_frame.h"
#include "media/base/video_renderer.h"
#include "media/filters/video_cadence_estimator.h"

namespace media {

// VideoRendererAlgorithm manages a queue of VideoFrames from which it chooses
// frames with the goal of providing a smooth playback experience.  I.e., the
// selection process results in the best possible uniformity for displayed frame
// durations over time.
//
// Clients will provide frames to VRA via EnqueueFrame() and then VRA will yield
// one of those frames in response to a future Render() call.  Each Render()
// call takes a render interval which is used to compute the best frame for
// display during that interval.
//
// Render() calls are expected to happen on a regular basis.  Failure to do so
// will result in suboptimal rendering experiences.  If a client knows that
// Render() callbacks are stalled for any reason, it should tell VRA to expire
// frames which are unusable via RemoveExpiredFrames(); this prevents useless
// accumulation of stale VideoFrame objects (which are frequently quite large).
//
// The primary means of smooth frame selection is via forced integer cadence,
// see VideoCadenceEstimator for details on this process.  In cases of non-
// integer cadence, the algorithm will fall back to choosing the frame which
// covers the most of the current render interval.  If no frame covers the
// current interval, the least bad frame will be chosen based on its drift from
// the start of the interval.
//
// Combined these three approaches enforce optimal smoothness in many cases.
class MEDIA_EXPORT VideoRendererAlgorithm {
 public:
  explicit VideoRendererAlgorithm(
      const TimeSource::WallClockTimeCB& wall_clock_time_cb);
  ~VideoRendererAlgorithm();

  // Chooses the best frame for the interval [deadline_min, deadline_max] based
  // on available and previously rendered frames.
  //
  // Under ideal circumstances the deadline interval provided to a Render() call
  // should be directly adjacent to the deadline given to the previous Render()
  // call with no overlap or gaps.  In practice, |deadline_max| is an estimated
  // value, which means the next |deadline_min| may overlap it slightly or have
  // a slight gap.  Gaps which exceed the length of the deadline interval are
  // assumed to be repeated frames for the purposes of cadence detection.
  //
  // If provided, |frames_dropped| will be set to the number of frames which
  // were removed from |frame_queue_|, during this call, which were never
  // returned during a previous Render() call and are no longer suitable for
  // rendering since their wall clock time is too far in the past.
  scoped_refptr<VideoFrame> Render(base::TimeTicks deadline_min,
                                   base::TimeTicks deadline_max,
                                   size_t* frames_dropped);

  // Removes all video frames which are unusable since their ideal render
  // interval [timestamp, timestamp + duration] is too far away from
  // |deadline_min| than is allowed by drift constraints.
  //
  // At least one frame will always remain after this call so that subsequent
  // Render() calls have a frame to return if no new frames are enqueued before
  // then.  Returns the number of frames removed.
  //
  // Note: In cases where there is no known frame duration (i.e. perhaps a video
  // with only a single frame), the last frame can not be expired, regardless of
  // the given deadline.  Clients must handle this case externally.
  size_t RemoveExpiredFrames(base::TimeTicks deadline);

  // Clients should call this if the last frame provided by Render() was never
  // rendered; it ensures the presented cadence matches internal models.  This
  // must be called before the next Render() call.
  void OnLastFrameDropped();

  // Adds a frame to |frame_queue_| for consideration by Render().  Out of order
  // timestamps will be sorted into appropriate order.  Do not enqueue end of
  // stream frames.  Frames inserted prior to the last rendered frame will not
  // be used.  They will be discarded on the next call to Render(), counting as
  // dropped frames, or by RemoveExpiredFrames(), counting as expired frames.
  //
  // Attempting to enqueue a frame with the same timestamp as a previous frame
  // will result in the previous frame being replaced if it has not been
  // rendered yet.  If it has been rendered, the new frame will be dropped.
  void EnqueueFrame(const scoped_refptr<VideoFrame>& frame);

  // Removes all frames from the |frame_queue_| and clears predictors.  The
  // algorithm will be as if freshly constructed after this call.
  void Reset();

  // Returns the number of frames currently buffered which could be rendered
  // assuming current Render() interval trends.  Before Render() is called, this
  // will be the same as the number of frames given to EnqueueFrame().  After
  // Render() has been called, one of two things will be returned:
  //
  // If a cadence has been identified, this will return the number of frames
  // which have a non-zero ideal render count.
  //
  // If cadence has not been identified, this will return the number of frames
  // which have a frame end time greater than the end of the last render
  // interval passed to Render().  Note: If Render() callbacks become suspended
  // and the duration is unknown the last frame may never be stop counting as
  // effective.  Clients must handle this case externally.
  //
  // In either case, frames enqueued before the last displayed frame will not
  // be counted as effective.
  size_t EffectiveFramesQueued() const;

  // Returns an estimate of the amount of memory (in bytes) used for frames.
  int64_t GetMemoryUsage() const;

  // Tells the algorithm that Render() callbacks have been suspended for a known
  // reason and such stoppage shouldn't be counted against future frames.
  void set_time_stopped() { was_time_moving_ = false; }

  size_t frames_queued() const { return frame_queue_.size(); }

  // Returns the average of the duration of all frames in |frame_queue_|
  // as measured in wall clock (not media) time.
  base::TimeDelta average_frame_duration() const {
    return average_frame_duration_;
  }

  // Method used for testing which disables frame dropping, in this mode the
  // algorithm will never drop frames and instead always return every frame
  // for display at least once.
  void disable_frame_dropping() { frame_dropping_disabled_ = true; }

 private:
  friend class VideoRendererAlgorithmTest;

  // The determination of whether to clamp to a given cadence is based on the
  // number of seconds before a frame would have to be dropped or repeated to
  // compensate for reaching the maximum acceptable drift.
  //
  // We've chosen 8 seconds based on practical observations and the fact that it
  // allows 29.9fps and 59.94fps in 60Hz and vice versa.
  //
  // Most users will not be able to see a single frame repeated or dropped every
  // 8 seconds and certainly should notice it less than the randomly variable
  // frame durations.
  static const int kMinimumAcceptableTimeBetweenGlitchesSecs = 8;

  // Metadata container for enqueued frames.  See |frame_queue_| below.
  struct ReadyFrame {
    ReadyFrame(const scoped_refptr<VideoFrame>& frame);
    ReadyFrame(const ReadyFrame& other);
    ~ReadyFrame();

    // For use with std::lower_bound.
    bool operator<(const ReadyFrame& other) const;

    scoped_refptr<VideoFrame> frame;

    // |start_time| is only available after UpdateFrameStatistics() has been
    // called and |end_time| only after we have more than one frame.
    base::TimeTicks start_time;
    base::TimeTicks end_time;

    // True if this frame's end time is based on the average frame duration and
    // not the time of the next frame.
    bool has_estimated_end_time;

    int ideal_render_count;
    int render_count;
    int drop_count;
  };

  // Updates the render count for the last rendered frame based on the number
  // of missing intervals between Render() calls.
  void AccountForMissedIntervals(base::TimeTicks deadline_min,
                                 base::TimeTicks deadline_max);

  // Updates the render count and wall clock timestamps for all frames in
  // |frame_queue_|.  Updates |was_time_stopped_|, |cadence_estimator_| and
  // |frame_duration_calculator_|.
  //
  // Note: Wall clock time is recomputed each Render() call because it's
  // expected that the TimeSource powering TimeSource::WallClockTimeCB will skew
  // slightly based on the audio clock.
  //
  // TODO(dalecurtis): Investigate how accurate we need the wall clock times to
  // be, so we can avoid recomputing every time (we would need to recompute when
  // playback rate changes occur though).
  void UpdateFrameStatistics();

  // Updates the ideal render count for all frames in |frame_queue_| based on
  // the cadence returned by |cadence_estimator_|.  Cadence is assigned based
  // on |frame_counter_|.
  void UpdateCadenceForFrames();

  // If |cadence_estimator_| has detected a valid cadence, attempts to find the
  // next frame which should be rendered.  Returns -1 if not enough frames are
  // available for cadence selection or there is no cadence.
  //
  // Returns the number of times a prior frame was over displayed and ate into
  // the returned frames ideal render count via |remaining_overage|.
  //
  // For example, if we have 2 frames and each has an ideal display count of 3,
  // but the first was displayed 4 times, the best frame is the second one, but
  // it should only be displayed twice instead of thrice, so it's overage is 1.
  int FindBestFrameByCadence(int* remaining_overage) const;

  // Iterates over |frame_queue_| and finds the frame which covers the most of
  // the deadline interval.  If multiple frames have coverage of the interval,
  // |second_best| will be set to the index of the frame with the next highest
  // coverage.  Returns -1 if no frame has any coverage of the current interval.
  //
  // Prefers the earliest frame if multiple frames have similar coverage (within
  // a few percent of each other).
  int FindBestFrameByCoverage(base::TimeTicks deadline_min,
                              base::TimeTicks deadline_max,
                              int* second_best) const;

  // Iterates over |frame_queue_| and find the frame which drifts the least from
  // |deadline_min|.  There's always a best frame by drift, so the return value
  // is always a valid frame index.  |selected_frame_drift| will be set to the
  // drift of the chosen frame.
  //
  // Note: Drift calculations assume contiguous frames in the time domain, so
  // it's not possible to have a case where a frame is -10ms from |deadline_min|
  // and another frame which is at some time after |deadline_min|.  The second
  // frame would be considered to start at -10ms before |deadline_min| and would
  // overlap |deadline_min|, so its drift would be zero.
  int FindBestFrameByDrift(base::TimeTicks deadline_min,
                           base::TimeDelta* selected_frame_drift) const;

  // Calculates the drift from |deadline_min| for the given |frame_index|.  If
  // the [start_time, end_time] lies before |deadline_min| the drift is
  // the delta between |deadline_min| and |end_time|. If the frame
  // overlaps |deadline_min| the drift is zero. If the frame lies after
  // |deadline_min| the drift is the delta between |deadline_min| and
  // |start_time|.
  base::TimeDelta CalculateAbsoluteDriftForFrame(base::TimeTicks deadline_min,
                                                 int frame_index) const;

  // Queue of incoming frames waiting for rendering.
  using VideoFrameQueue = std::deque<ReadyFrame>;
  VideoFrameQueue frame_queue_;

  // The index of the last frame rendered; presumed to be the first frame if no
  // frame has been rendered yet. Updated by Render() and EnqueueFrame() if any
  // frames are added or removed.
  //
  // In most cases this value is zero, but when out of order timestamps are
  // present, the last rendered frame may be moved.
  size_t last_frame_index_;

  // Handles cadence detection and frame cadence assignments.
  VideoCadenceEstimator cadence_estimator_;

  // Indicates if any calls to Render() have successfully yielded a frame yet.
  bool have_rendered_frames_;

  // Callback used to convert media timestamps into wall clock timestamps.
  const TimeSource::WallClockTimeCB wall_clock_time_cb_;

  // The last |deadline_max| provided to Render(), used to predict whether
  // frames were rendered over cadence between Render() calls.
  base::TimeTicks last_deadline_max_;

  // The average of the duration of all frames in |frame_queue_| as measured in
  // wall clock (not media) time at the time of the last Render().
  MovingAverage frame_duration_calculator_;
  base::TimeDelta average_frame_duration_;

  // The length of the last deadline interval given to Render(), updated at the
  // start of Render().
  base::TimeDelta render_interval_;

  // The maximum acceptable drift before a frame can no longer be considered for
  // rendering within a given interval.
  base::TimeDelta max_acceptable_drift_;

  // Indicates that the last call to Render() experienced a rendering glitch; it
  // may have: under-rendered a frame, over-rendered a frame, dropped one or
  // more frames, or chosen a frame which exceeded acceptable drift.
  bool last_render_had_glitch_;

  // For testing functionality which enables clockless playback of all frames,
  // does not prevent frame dropping due to equivalent timestamps.
  bool frame_dropping_disabled_;

  // Tracks frames dropped during enqueue when identical timestamps are added
  // to the queue.  Callers are told about these frames during Render().
  size_t frames_dropped_during_enqueue_;

  // When cadence is present, we don't want to start counting against cadence
  // until the first frame has reached its presentation time.
  bool first_frame_;

  // The frame number of the last rendered frame; incremented for every frame
  // rendered and every frame dropped or expired since the last rendered frame.
  //
  // Given to |cadence_estimator_| when assigning cadence values to the
  // ReadyFrameQueue.  Cleared when a new cadence is detected.
  uint64_t cadence_frame_counter_;

  // Tracks whether the last call to Render() choose to ignore the frame chosen
  // by cadence in favor of one by drift or coverage.
  bool last_render_ignored_cadence_frame_;

  // Indicates if time was moving, set to the return value from
  // UpdateFrameStatistics() during Render() or externally by
  // set_time_stopped().
  bool was_time_moving_;

  DISALLOW_COPY_AND_ASSIGN(VideoRendererAlgorithm);
};

}  // namespace media

#endif  // MEDIA_FILTERS_VIDEO_RENDERER_ALGORITHM_H_
