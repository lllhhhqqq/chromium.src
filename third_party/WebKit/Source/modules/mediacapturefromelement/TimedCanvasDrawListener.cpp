// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modules/mediacapturefromelement/TimedCanvasDrawListener.h"

namespace blink {

TimedCanvasDrawListener::TimedCanvasDrawListener(const PassOwnPtr<WebCanvasCaptureHandler>& handler, double frameRate)
    : CanvasDrawListener(handler)
    , m_frameInterval(1 / frameRate)
    , m_requestFrameTimer(this, &TimedCanvasDrawListener::requestFrameTimerFired)
{
}

TimedCanvasDrawListener::~TimedCanvasDrawListener() {}

// static
TimedCanvasDrawListener* TimedCanvasDrawListener::create(const PassOwnPtr<WebCanvasCaptureHandler>& handler, double frameRate)
{
    TimedCanvasDrawListener* listener = new TimedCanvasDrawListener(handler, frameRate);
    listener->m_requestFrameTimer.startRepeating(listener->m_frameInterval, BLINK_FROM_HERE);
    return listener;
}

void TimedCanvasDrawListener::sendNewFrame(const WTF::PassRefPtr<SkImage>& image)
{
    m_frameCaptureRequested = false;
    CanvasDrawListener::sendNewFrame(image);
}

void TimedCanvasDrawListener::requestFrameTimerFired(Timer<TimedCanvasDrawListener>*)
{
    // TODO(emircan): Measure the jitter and log, see crbug.com/589974.
    m_frameCaptureRequested = true;
}

} // namespace blink
