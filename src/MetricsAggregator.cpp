// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT

#include "MetricsAggregator.hpp"

#include "PerformanceCountersToDuration.hpp"

static auto operator-(const LARGE_INTEGER& lhs, const LARGE_INTEGER& rhs) {
  return PerformanceCountersToDuration(lhs.QuadPart - rhs.QuadPart);
}

#define MEAN_METRICS(OP) \
  OP(WaitCpu) \
  OP(RuntimeCpu) \
  OP(RenderCpu)

void MetricsAggregator::Push(const FramePerformanceCounters& fpc) {
  ++mAccumulator.mFrameCount;
  if (mPreviousFrameEndTime.QuadPart) {
    mAccumulator.mSincePreviousFrame
      += fpc.mEndFrameStop - mPreviousFrameEndTime;
    if (fpc.mWaitFrameStart.QuadPart) {
      mAccumulator.mAppCpu += fpc.mWaitFrameStart - mPreviousFrameEndTime;
    }
  }
  mPreviousFrameEndTime = fpc.mEndFrameStop;

  if (!fpc.mWaitFrameStart.QuadPart) {
    mHavePartialData = true;
  }

  if (mHavePartialData) {
    return;
  }

  mAccumulator.mAppCpu += fpc.mBeginFrameStart - fpc.mWaitFrameStop;
  mAccumulator.mAppCpu += fpc.mEndFrameStart - fpc.mBeginFrameStop;

  const FrameMetrics fm {fpc};

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
  const auto ret = std::move(mAccumulator);
  mAccumulator = {};
  mHavePartialData = false;
  return ret;
}
