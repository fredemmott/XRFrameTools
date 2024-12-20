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
}// namespace

MetricsAggregator::MetricsAggregator(const PerformanceCounterMath& pc)
  : mPerformanceCounterMath(pc) {
}

void MetricsAggregator::Push(const FramePerformanceCounters& rawFpc) {
  if (!rawFpc.mBeginFrameStart.QuadPart) {
    // We couldn't match the predicted display time in xrEndFrame,
    // so all core stats are bogus
    //
    // For example, this happens if OpenXR Toolkit is running turbo mode
    // in a layer closer to the game
    return;
  }
  if (rawFpc.mEndFrameStop.QuadPart && !mPreviousFrameEndTime.QuadPart) {
    // While the frame is overall valid, without an interval (and FPS)
    // we can't draw useful conclusions from it
    mPreviousFrameEndTime = rawFpc.mEndFrameStop;
    return;
  }
  if (rawFpc.mEndFrameStop.QuadPart < mPreviousFrameEndTime.QuadPart) {
    // out-of-order frames
#ifndef _NDEBUG
    __debugbreak();
#endif
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
  SetIfLarger(&fpc.mWaitFrameStart, mPreviousFrameEndTime);
  SetIfLarger(&fpc.mWaitFrameStop, mPreviousFrameEndTime);
  SetIfLarger(&fpc.mBeginFrameStart, mPreviousFrameEndTime);
  SetIfLarger(&fpc.mBeginFrameStop, mPreviousFrameEndTime);

  auto& acc = mAccumulator;

  if (++mAccumulator.mFrameCount == 1) {
    acc.mValidDataBits = fpc.mValidDataBits;
    acc.mGpuLowestPState = fpc.mGpuPerformanceInformation.mPState;
    // Highest handled by max(), lowest isn't as default is 0
  } else {
    acc.mValidDataBits &= fpc.mValidDataBits;
  }

  const auto& pcm = mPerformanceCounterMath;

  acc.mWaitCpu += pcm.ToDuration(fpc.mWaitFrameStart, fpc.mWaitFrameStop);
  acc.mRenderCpu += pcm.ToDuration(fpc.mBeginFrameStop, fpc.mEndFrameStart);
  acc.mRuntimeCpu += pcm.ToDuration(fpc.mBeginFrameStart, fpc.mBeginFrameStop)
    + pcm.ToDuration(fpc.mEndFrameStart, fpc.mEndFrameStop);

  acc.mAppCpu += pcm.ToDuration(mPreviousFrameEndTime, fpc.mWaitFrameStart)
    + pcm.ToDuration(fpc.mWaitFrameStop, fpc.mBeginFrameStart);

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
  acc.mGpuLowestPState
    = std::min(acc.mGpuLowestPState, fpc.mGpuPerformanceInformation.mPState);
  acc.mGpuHighestPState
    = std::max(acc.mGpuHighestPState, fpc.mGpuPerformanceInformation.mPState);

  acc.mSincePreviousFrame
    += pcm.ToDuration(mPreviousFrameEndTime, fpc.mEndFrameStop);
  mPreviousFrameEndTime = fpc.mEndFrameStop;
}

std::optional<FrameMetrics> MetricsAggregator::Flush() {
  auto& acc = mAccumulator;
  const auto n = acc.mFrameCount;
  if (n == 0) {
    return std::nullopt;
  }

  acc.mSincePreviousFrame /= n;
  acc.mWaitCpu /= n;
  acc.mRenderCpu /= n;
  acc.mRuntimeCpu /= n;
  acc.mAppCpu /= n;

  acc.mRenderGpu /= n;

  mHavePartialData = false;
  return std::exchange(mAccumulator, {});
}
