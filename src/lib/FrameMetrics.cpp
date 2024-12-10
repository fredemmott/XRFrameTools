// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT

#include "FrameMetrics.hpp"

#include "FramePerformanceCounters.hpp"
#include "PerformanceCounterMath.hpp"

FrameMetrics::FrameMetrics(
  const PerformanceCounterMath& pcm,
  const FramePerformanceCounters& fpc) {
  if (!fpc.mBeginFrameStart.QuadPart) {
    // We couldn't match the predicted display time in xrEndFrame,
    // so all core stats are bogus
    //
    // For example, this happens if OpenXR Toolkit is running turbo mode
    // in a layer closer to the game
    return;
  }

  mWaitCpu = pcm.ToDuration(fpc.mWaitFrameStart, fpc.mWaitFrameStop);
  mRenderCpu = pcm.ToDuration(fpc.mBeginFrameStop, fpc.mEndFrameStart);
  mRuntimeCpu = pcm.ToDuration(fpc.mBeginFrameStart, fpc.mBeginFrameStop)
    + pcm.ToDuration(fpc.mEndFrameStart, fpc.mEndFrameStop);
}
