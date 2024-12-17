// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT

#include "MetricsAggregator.hpp"

#include "FrameMetrics.hpp"
#include "FramePerformanceCounters.hpp"

#define MEAN_METRICS(OP) \
  OP(WaitCpu) \
  OP(RuntimeCpu) \
  OP(RenderCpu) \
  OP(RenderGpu) \
  OP(VideoMemoryInfo.Budget) \
  OP(VideoMemoryInfo.CurrentUsage) \
  OP(VideoMemoryInfo.AvailableForReservation) \
  OP(VideoMemoryInfo.CurrentReservation)

MetricsAggregator::MetricsAggregator(const PerformanceCounterMath& pc)
  : mPerformanceCounterMath(pc) {
}

static void AddIfOrdered(
  const PerformanceCounterMath& pcm,
  std::chrono::microseconds* acc,
  LARGE_INTEGER* begin,
  LARGE_INTEGER end,
  LARGE_INTEGER newBegin) {
  if (begin->QuadPart > end.QuadPart) {
    return;
  }
  *acc += pcm.ToDuration(*begin, end);
  *begin = newBegin;
}

void MetricsAggregator::Push(const FramePerformanceCounters& fpc) {
  if (++mAccumulator.mFrameCount == 1) {
    mAccumulator.mValidDataBits = fpc.mValidDataBits;

    mAccumulator.mGpuPerformanceInfo = fpc.mGpuPerformanceInformation;
    mAccumulator.mGpuLowestPState = fpc.mGpuPerformanceInformation.mPState;
    mAccumulator.mGpuHighestPState = fpc.mGpuPerformanceInformation.mPState;
  } else {
    mAccumulator.mValidDataBits &= fpc.mValidDataBits;

    mAccumulator.mGpuPerformanceInfo.mDecreaseReason
      |= fpc.mGpuPerformanceInformation.mDecreaseReason;
    mAccumulator.mGpuLowestPState = std::min(
      mAccumulator.mGpuLowestPState, fpc.mGpuPerformanceInformation.mPState);
    mAccumulator.mGpuHighestPState = std::max(
      mAccumulator.mGpuHighestPState, fpc.mGpuPerformanceInformation.mPState);
  }

  const auto pcm = mPerformanceCounterMath;
  std::chrono::microseconds appCpu {};

  if (mPreviousFrameEndTime.QuadPart > fpc.mEndFrameStart.QuadPart) {
#ifndef NDEBUG
    //__debugbreak();
#endif
    return;
  }

  const auto interval
    = pcm.ToDuration(mPreviousFrameEndTime, fpc.mEndFrameStop);

  if (mPreviousFrameEndTime.QuadPart) {
    mAccumulator.mSincePreviousFrame += interval;

    auto start = mPreviousFrameEndTime;
    AddIfOrdered(pcm, &appCpu, &start, fpc.mWaitFrameStart, fpc.mWaitFrameStop);
    AddIfOrdered(
      pcm, &appCpu, &start, fpc.mBeginFrameStart, fpc.mBeginFrameStop);
    // Begin -> End is 'render CPU'
  }
  if (!(fpc.mWaitFrameStart.QuadPart)) {
    mHavePartialData = true;
  }

  mPreviousFrameEndTime = fpc.mEndFrameStop;

  if (mHavePartialData) {
    return;
  }

  const FrameMetrics fm {pcm, fpc};
  if (appCpu > interval) {
#ifndef NDEBUG
    __debugbreak();
#endif
    appCpu = interval - (fm.mRenderCpu + fm.mWaitCpu + fm.mRenderCpu);
  }
  mAccumulator.mAppCpu += appCpu;

#define ADD_METRIC(X) mAccumulator.m##X += fm.m##X;
  MEAN_METRICS(ADD_METRIC)
#undef ADD_METRIC
}

std::optional<AggregatedFrameMetrics> MetricsAggregator::Flush() {
  if (mAccumulator.mFrameCount == 0) {
    return std::nullopt;
  }

#define DIVIDE_METRIC(X) mAccumulator.m##X /= mAccumulator.mFrameCount;
#define CLEAR_METRIC(X) mAccumulator.m##X = {};
  if (mHavePartialData) {
    MEAN_METRICS(CLEAR_METRIC)
    CLEAR_METRIC(AppCpu)
  } else {
    MEAN_METRICS(DIVIDE_METRIC)
    DIVIDE_METRIC(AppCpu)
  }
  DIVIDE_METRIC(SincePreviousFrame)
#undef DIVIDE_METRIC
#undef CLEAR_METRIC
  mHavePartialData = false;
  return std::exchange(mAccumulator, {});
}
