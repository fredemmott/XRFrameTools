// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT

#include "FrameMetrics.hpp"

#include "FramePerformanceCounters.hpp"
#include "PerformanceCountersToDuration.hpp"

static auto operator-(const LARGE_INTEGER& lhs, const LARGE_INTEGER& rhs) {
  return PerformanceCountersToDuration(lhs.QuadPart - rhs.QuadPart);
}

FrameMetrics::FrameMetrics(const FramePerformanceCounters& fpc) {
  if (!fpc.mBeginFrameStart.QuadPart) {
    // We couldn't match the predicted display time in xrEndFrame,
    // so all core stats are bogus
    //
    // For example, this happens if OpenXR Toolkit is running turbo mode
    // in a layer closer to the game
    return;
  }

  mWaitCpu = fpc.mWaitFrameStop - fpc.mWaitFrameStart;
  mRenderCpu = fpc.mEndFrameStart - fpc.mBeginFrameStop;
  mRuntimeCpu = (fpc.mBeginFrameStop - fpc.mBeginFrameStart)
    + (fpc.mEndFrameStop - fpc.mEndFrameStart);
}
