// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT

#include "FrameMetrics.hpp"

#include "FramePerformanceCounters.hpp"
#include "PerformanceCountersToDuration.hpp"

static auto operator-(const LARGE_INTEGER& lhs, const LARGE_INTEGER& rhs) {
  return PerformanceCountersToDuration(lhs.QuadPart - rhs.QuadPart);
}

FrameMetrics::FrameMetrics(const FramePerformanceCounters& fpc) {
  mWait = fpc.mWaitFrameStop - fpc.mWaitFrameStart;
  mAppCpu = fpc.mEndFrameStart - fpc.mBeginFrameStop;
  mRuntimeCpu = (fpc.mBeginFrameStop - fpc.mBeginFrameStart)
    + (fpc.mEndFrameStop - fpc.mEndFrameStart);
  mTotalCpu = fpc.mEndFrameStop - fpc.mWaitFrameStart;
}
