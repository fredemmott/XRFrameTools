// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT
#pragma once

#include <optional>

#include "FrameMetrics.hpp"
#include "PerformanceCounterMath.hpp"

struct AggregatedFrameMetrics : FrameMetrics {
  uint16_t mFrameCount {};
  std::chrono::microseconds mAppCpu {};
  std::chrono::microseconds mSincePreviousFrame {};
};

class MetricsAggregator final {
 public:
  MetricsAggregator() = delete;
  MetricsAggregator(const MetricsAggregator&) = delete;
  MetricsAggregator(MetricsAggregator&&) = delete;
  MetricsAggregator& operator=(const MetricsAggregator&) = delete;
  MetricsAggregator& operator=(MetricsAggregator&&) = delete;

  MetricsAggregator(const PerformanceCounterMath&);

  void Push(const FramePerformanceCounters&);
  [[nodiscard]] std::optional<AggregatedFrameMetrics> Flush();

  void Reset() {
    auto pcm = mPerformanceCounterMath;
    this->~MetricsAggregator();
    new (this) MetricsAggregator(mPerformanceCounterMath);
  }

 private:
  const PerformanceCounterMath mPerformanceCounterMath;

  AggregatedFrameMetrics mAccumulator {};
  LARGE_INTEGER mPreviousFrameEndTime {};
  bool mHavePartialData = false;
};