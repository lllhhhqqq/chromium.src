/*
 * Copyright (C) 2011, Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.  Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "modules/webaudio/WaveShaperDSPKernel.h"
#include "wtf/Threading.h"
#include <algorithm>

const unsigned RenderingQuantum = 128;

namespace blink {

WaveShaperDSPKernel::WaveShaperDSPKernel(WaveShaperProcessor* processor)
    : AudioDSPKernel(processor)
{
    if (processor->oversample() != WaveShaperProcessor::OverSampleNone)
        lazyInitializeOversampling();
}

void WaveShaperDSPKernel::lazyInitializeOversampling()
{
    if (!m_tempBuffer) {
        m_tempBuffer = adoptPtr(new AudioFloatArray(RenderingQuantum * 2));
        m_tempBuffer2 = adoptPtr(new AudioFloatArray(RenderingQuantum * 4));
        m_upSampler = adoptPtr(new UpSampler(RenderingQuantum));
        m_downSampler = adoptPtr(new DownSampler(RenderingQuantum * 2));
        m_upSampler2 = adoptPtr(new UpSampler(RenderingQuantum * 2));
        m_downSampler2 = adoptPtr(new DownSampler(RenderingQuantum * 4));
    }
}

void WaveShaperDSPKernel::process(const float* source, float* destination, size_t framesToProcess)
{
    switch (getWaveShaperProcessor()->oversample()) {
    case WaveShaperProcessor::OverSampleNone:
        processCurve(source, destination, framesToProcess);
        break;
    case WaveShaperProcessor::OverSample2x:
        processCurve2x(source, destination, framesToProcess);
        break;
    case WaveShaperProcessor::OverSample4x:
        processCurve4x(source, destination, framesToProcess);
        break;

    default:
        ASSERT_NOT_REACHED();
    }
}

void WaveShaperDSPKernel::processCurve(const float* source, float* destination, size_t framesToProcess)
{
    ASSERT(source);
    ASSERT(destination);
    ASSERT(getWaveShaperProcessor());

    DOMFloat32Array* curve = getWaveShaperProcessor()->curve();
    if (!curve) {
        // Act as "straight wire" pass-through if no curve is set.
        memcpy(destination, source, sizeof(float) * framesToProcess);
        return;
    }

    float* curveData = curve->data();
    int curveLength = curve->length();

    ASSERT(curveData);

    if (!curveData || !curveLength) {
        memcpy(destination, source, sizeof(float) * framesToProcess);
        return;
    }

    // Apply waveshaping curve.
    for (unsigned i = 0; i < framesToProcess; ++i) {
        const float input = source[i];

        // Calculate a virtual index based on input -1 -> +1 with -1 being curve[0], +1 being
        // curve[curveLength - 1], and 0 being at the center of the curve data. Then linearly
        // interpolate between the two points in the curve.
        double virtualIndex = 0.5 * (input + 1) * (curveLength - 1);
        double output;

        if (virtualIndex < 0) {
            // input < -1, so use curve[0]
            output = curveData[0];
        } else if (virtualIndex >= curveLength - 1) {
            // input >= 1, so use last curve value
            output = curveData[curveLength - 1];
        } else {
            // The general case where -1 <= input < 1, where 0 <= virtualIndex < curveLength - 1,
            // so interpolate between the nearest samples on the curve.
            unsigned index1 = static_cast<unsigned>(virtualIndex);
            unsigned index2 = index1 + 1;
            double interpolationFactor = virtualIndex - index1;

            double value1 = curveData[index1];
            double value2 = curveData[index2];

            output = (1.0 - interpolationFactor) * value1 + interpolationFactor * value2;
        }
        destination[i] = output;
    }
}

void WaveShaperDSPKernel::processCurve2x(const float* source, float* destination, size_t framesToProcess)
{
    bool isSafe = framesToProcess == RenderingQuantum;
    ASSERT(isSafe);
    if (!isSafe)
        return;

    float* tempP = m_tempBuffer->data();

    m_upSampler->process(source, tempP, framesToProcess);

    // Process at 2x up-sampled rate.
    processCurve(tempP, tempP, framesToProcess * 2);

    m_downSampler->process(tempP, destination, framesToProcess * 2);
}

void WaveShaperDSPKernel::processCurve4x(const float* source, float* destination, size_t framesToProcess)
{
    bool isSafe = framesToProcess == RenderingQuantum;
    ASSERT(isSafe);
    if (!isSafe)
        return;

    float* tempP = m_tempBuffer->data();
    float* tempP2 = m_tempBuffer2->data();

    m_upSampler->process(source, tempP, framesToProcess);
    m_upSampler2->process(tempP, tempP2, framesToProcess * 2);

    // Process at 4x up-sampled rate.
    processCurve(tempP2, tempP2, framesToProcess * 4);

    m_downSampler2->process(tempP2, tempP, framesToProcess * 4);
    m_downSampler->process(tempP, destination, framesToProcess * 2);
}

void WaveShaperDSPKernel::reset()
{
    if (m_upSampler) {
        m_upSampler->reset();
        m_downSampler->reset();
        m_upSampler2->reset();
        m_downSampler2->reset();
    }
}

double WaveShaperDSPKernel::latencyTime() const
{
    size_t latencyFrames = 0;
    WaveShaperDSPKernel* kernel = const_cast<WaveShaperDSPKernel*>(this);

    switch (kernel->getWaveShaperProcessor()->oversample()) {
    case WaveShaperProcessor::OverSampleNone:
        break;
    case WaveShaperProcessor::OverSample2x:
        latencyFrames += m_upSampler->latencyFrames();
        latencyFrames += m_downSampler->latencyFrames();
        break;
    case WaveShaperProcessor::OverSample4x:
        {
            // Account for first stage upsampling.
            latencyFrames += m_upSampler->latencyFrames();
            latencyFrames += m_downSampler->latencyFrames();

            // Account for second stage upsampling.
            // and divide by 2 to get back down to the regular sample-rate.
            size_t latencyFrames2 = (m_upSampler2->latencyFrames() + m_downSampler2->latencyFrames()) / 2;
            latencyFrames += latencyFrames2;
            break;
        }
    default:
        ASSERT_NOT_REACHED();
    }

    return static_cast<double>(latencyFrames) / sampleRate();
}

} // namespace blink

