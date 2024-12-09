// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT

#include "MetricsAggregator.hpp"

#include "PerformanceCountersToDuration.hpp"

static auto operator-(const LARGE_INTEGER& lhs, const LARGE_INTEGER& rhs) {
  return PerformanceCountersToDuration(lhs.QuadPart - rhs.QuadPart);
}

#define MEAN_METRICS(OP) \
  OP(Wait) \
  OP(AppCpu) \
  OP(RuntimeCpu) \
  OP(TotalCpu)

void MetricsAggregator::Push(const FramePerformanceCounters& fpc) {
  const FrameMetrics fm {fpc};
  if (mAccumulator.mFrameCount == 0) {
    mAccumulator = {fm};
    mAccumulator.mFrameCount = 1;

    if (mPreviousFrameEndTime.QuadPart) {
      mAccumulator.mSincePreviousFrame
        = fpc.mEndFrameStop - mPreviousFrameEndTime;
    }
    mPreviousFrameEndTime = fpc.mEndFrameStop;
    return;
  }

  ++mAccumulator.mFrameCount;

#define ADD_METRIC(X) mAccumulator.m##X += fm.m##X;
  MEAN_METRICS(ADD_METRIC)
#undef ADD_METRIC

  mAccumulator.mSincePreviousFrame
    += (fpc.mEndFrameStop - mPreviousFrameEndTime);
  mPreviousFrameEndTime = fpc.mEndFrameStop;
}

std::optional<AggregatedFrameMetrics> MetricsAggregator::Flush() {
  if (mAccumulator.mFrameCount == 0) {
    return std::nullopt;
  }

#define DIVIDE_METRIC(X) mAccumulator.m##X /= mAccumulator.mFrameCount;
  MEAN_METRICS(DIVIDE_METRIC)
  DIVIDE_METRIC(SincePreviousFrame)
#undef DIVIDE_METRIC
  const auto ret = std::move(mAccumulator);
  mAccumulator = {};
  return ret;
}
