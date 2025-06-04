// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT

#include "MetricsAggregator.hpp"

#include "FrameMetrics.hpp"
#include "FramePerformanceCounters.hpp"

namespace {
template <class T>
void SetIfLarger(T* a, T b) {
  if (b > *a) {
    *a = b;
  }
}

template <>
void SetIfLarger<LARGE_INTEGER>(LARGE_INTEGER* a, LARGE_INTEGER b) {
  SetIfLarger(&a->QuadPart, b.QuadPart);
}

template <class T>
void SetIfSmallerOrTargetIsZero(T* a, T b) {
  if (*a == 0 || b < *a) {
    *a = b;
  }
}

}// namespace

MetricsAggregator::MetricsAggregator(const PerformanceCounterMath& pc)
  : mPerformanceCounterMath(pc) {
}

void MetricsAggregator::Push(const FramePerformanceCounters& rawFpc) {
  const auto& rawCore = rawFpc.mCore;
  if (!rawCore.mBeginFrameStart.QuadPart) {
    // We couldn't match the predicted display time in xrEndFrame,
    // so all core stats are bogus
    //
    // For example, this happens if OpenXR Toolkit is running turbo mode
    // in a layer closer to the game
    return;
  }
  if (rawCore.mEndFrameStop.QuadPart && !mPreviousFrameEndTime.QuadPart) {
    // While the frame is overall valid, without an interval (and FPS)
    // we can't draw useful conclusions from it
    mPreviousFrameEndTime = rawCore.mEndFrameStop;
    return;
  }
  if (rawCore.mEndFrameStop.QuadPart < mPreviousFrameEndTime.QuadPart) {
    mPreviousFrameEndTime = {};
    mAccumulator = {};
    mHavePartialData = false;
    return;
  }

  // Normalize so nothing starts before the previous frame is submitted; this
  // effectively 'flattens' the timing diagram, discarding anything that
  // overlaps.
  //
  // Overlaps are normal in multithreading. For XRFrameTools, we try to show
  // what's blocking the render loop, not actual time spent on the frame.
  //
  // Actual time on the frame needs in-engine metrics and/or profiling tools
  auto fpc = rawFpc;
  auto& core = fpc.mCore;
  SetIfLarger(&core.mWaitFrameStart, mPreviousFrameEndTime);
  SetIfLarger(&core.mWaitFrameStop, mPreviousFrameEndTime);
  SetIfLarger(&core.mBeginFrameStart, mPreviousFrameEndTime);
  SetIfLarger(&core.mBeginFrameStop, mPreviousFrameEndTime);

  auto& acc = mAccumulator;
  SetIfLarger(&acc.mLastXrDisplayTime, core.mXrDisplayTime);

  if (++mAccumulator.mFrameCount == 1) {
    acc.mValidDataBits = fpc.mValidDataBits;
    acc.mGpuPStateMin = fpc.mGpuPerformanceInformation.mPState;
    // Highest handled by max(), lowest isn't as default is 0
  } else {
    acc.mValidDataBits &= fpc.mValidDataBits;
  }

  const auto& pcm = mPerformanceCounterMath;

  acc.mWaitFrameCpu
    += pcm.ToDuration(core.mWaitFrameStart, core.mWaitFrameStop);
  acc.mRenderCpu += pcm.ToDuration(core.mBeginFrameStop, core.mEndFrameStart);
  acc.mBeginFrameCpu
    += pcm.ToDuration(core.mBeginFrameStart, core.mBeginFrameStop);
  acc.mEndFrameCpu = pcm.ToDuration(core.mEndFrameStart, core.mEndFrameStop);

  acc.mAppCpu += pcm.ToDuration(mPreviousFrameEndTime, core.mWaitFrameStart)
    + pcm.ToDuration(core.mWaitFrameStop, core.mBeginFrameStart);

  acc.mRenderGpu += std::chrono::microseconds(fpc.mRenderGpu);

  SetIfLarger(&acc.mVideoMemoryInfo.Budget, fpc.mVideoMemoryInfo.Budget);
  SetIfLarger(
    &acc.mVideoMemoryInfo.CurrentUsage, fpc.mVideoMemoryInfo.CurrentUsage);
  SetIfLarger(
    &acc.mVideoMemoryInfo.AvailableForReservation,
    fpc.mVideoMemoryInfo.AvailableForReservation);
  SetIfLarger(
    &acc.mVideoMemoryInfo.CurrentReservation,
    fpc.mVideoMemoryInfo.CurrentReservation);

  acc.mGpuPerformanceDecreaseReasons
    |= fpc.mGpuPerformanceInformation.mDecreaseReasons;
  acc.mGpuPStateMin
    = std::min(acc.mGpuPStateMin, fpc.mGpuPerformanceInformation.mPState);
  acc.mGpuPStateMax
    = std::max(acc.mGpuPStateMax, fpc.mGpuPerformanceInformation.mPState);

  SetIfSmallerOrTargetIsZero(
    &acc.mGpuGraphicsKHzMin, fpc.mGpuPerformanceInformation.mGraphicsKHz);
  SetIfSmallerOrTargetIsZero(
    &acc.mGpuMemoryKHzMin, fpc.mGpuPerformanceInformation.mMemoryKHz);

  SetIfLarger(
    &acc.mGpuGraphicsKHzMax, fpc.mGpuPerformanceInformation.mGraphicsKHz);
  SetIfLarger(&acc.mGpuMemoryKHzMax, fpc.mGpuPerformanceInformation.mMemoryKHz);

  acc.mSincePreviousFrame
    += pcm.ToDuration(mPreviousFrameEndTime, core.mEndFrameStop);
  mPreviousFrameEndTime = core.mEndFrameStop;
}

std::optional<FrameMetrics> MetricsAggregator::Flush() {
  auto& acc = mAccumulator;
  const auto n = acc.mFrameCount;
  if (n == 0) {
    return std::nullopt;
  }

  acc.mSincePreviousFrame /= n;
  acc.mWaitFrameCpu /= n;
  acc.mRenderCpu /= n;
  acc.mBeginFrameCpu /= n;
  acc.mEndFrameCpu /= n;
  acc.mAppCpu /= n;

  acc.mRenderGpu /= n;

  mHavePartialData = false;
  return std::exchange(mAccumulator, {});
}
