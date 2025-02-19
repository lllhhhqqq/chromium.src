// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_CAPTURE_CONTENT_THREAD_SAFE_CAPTURE_ORACLE_H_
#define MEDIA_CAPTURE_CONTENT_THREAD_SAFE_CAPTURE_ORACLE_H_

#include <string>

#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "media/base/media_export.h"
#include "media/base/video_frame.h"
#include "media/capture/content/video_capture_oracle.h"
#include "media/capture/video/video_capture_device.h"

namespace tracked_objects {
class Location;
}  // namespace tracked_objects

namespace media {

struct VideoCaptureParams;
class VideoFrame;

// Thread-safe, refcounted proxy to the VideoCaptureOracle.  This proxy wraps
// the VideoCaptureOracle, which decides which frames to capture, and a
// VideoCaptureDevice::Client, which allocates and receives the captured
// frames, in a lock to synchronize state between the two.
class MEDIA_EXPORT ThreadSafeCaptureOracle
    : public base::RefCountedThreadSafe<ThreadSafeCaptureOracle> {
 public:
  ThreadSafeCaptureOracle(scoped_ptr<VideoCaptureDevice::Client> client,
                          const VideoCaptureParams& params,
                          bool enable_auto_throttling);

  // Called when a captured frame is available or an error has occurred.
  // If |success| is true then |frame| is valid and |timestamp| indicates when
  // the frame was painted.
  // If |success| is false, all other parameters are invalid.
  typedef base::Callback<void(const scoped_refptr<VideoFrame>& frame,
                              base::TimeTicks timestamp,
                              bool success)> CaptureFrameCallback;

  // Record a change |event| along with its |damage_rect| and |event_time|, and
  // then make a decision whether to proceed with capture. The decision is based
  // on recent event history, capture activity, and the availability of
  // resources.
  //
  // If this method returns false, the caller should take no further action.
  // Otherwise, |storage| is set to the destination for the video frame capture
  // and the caller should initiate capture.  Then, once the video frame has
  // been populated with its content, or if capture failed, the |callback|
  // should be run.
  bool ObserveEventAndDecideCapture(VideoCaptureOracle::Event event,
                                    const gfx::Rect& damage_rect,
                                    base::TimeTicks event_time,
                                    scoped_refptr<VideoFrame>* storage,
                                    CaptureFrameCallback* callback);

  // Attempt to re-send the last frame to the VideoCaptureDevice::Client.
  // Returns true if successful. This can fail if the last frame is no longer
  // available in the buffer pool, or if the VideoCaptureOracle decides to
  // reject the "passive" refresh.
  bool AttemptPassiveRefresh();

  base::TimeDelta min_capture_period() const {
    return oracle_.min_capture_period();
  }

  base::TimeTicks last_time_animation_was_detected() const {
    return oracle_.last_time_animation_was_detected();
  }

  gfx::Size max_frame_size() const {
    return params_.requested_format.frame_size;
  }

  // Returns the current capture resolution.
  gfx::Size GetCaptureSize() const;

  // Updates capture resolution based on the supplied source size and the
  // maximum frame size.
  void UpdateCaptureSize(const gfx::Size& source_size);

  // Stop new captures from happening (but doesn't forget the client).
  void Stop();

  // Signal an error to the client.
  void ReportError(const tracked_objects::Location& from_here,
                   const std::string& reason);

 private:
  friend class base::RefCountedThreadSafe<ThreadSafeCaptureOracle>;
  virtual ~ThreadSafeCaptureOracle();

  // Callback invoked on completion of all captures.
  void DidCaptureFrame(int frame_number,
                       scoped_ptr<VideoCaptureDevice::Client::Buffer> buffer,
                       base::TimeTicks capture_begin_time,
                       base::TimeDelta estimated_frame_duration,
                       const scoped_refptr<VideoFrame>& frame,
                       base::TimeTicks timestamp,
                       bool success);

  // Callback invoked once all consumers have finished with a delivered video
  // frame.  Consumer feedback signals are scanned from the frame's |metadata|.
  void DidConsumeFrame(int frame_number,
                       const media::VideoFrameMetadata* metadata);

  // Protects everything below it.
  mutable base::Lock lock_;

  // Recipient of our capture activity.
  scoped_ptr<VideoCaptureDevice::Client> client_;

  // Makes the decision to capture a frame.
  VideoCaptureOracle oracle_;

  // The video capture parameters used to construct the oracle proxy.
  const VideoCaptureParams params_;
};

}  // namespace media

#endif  // MEDIA_CAPTURE_CONTENT_THREAD_SAFE_CAPTURE_ORACLE_H_
