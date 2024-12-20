// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT
#pragma once

#include <optional>

#include "FrameMetrics.hpp"
#include "FramePerformanceCounters.hpp"
#include "PerformanceCounterMath.hpp"

class MetricsAggregator final {
 public:
  MetricsAggregator() = delete;
  MetricsAggregator(const MetricsAggregator&) = delete;
  MetricsAggregator(MetricsAggregator&&) = delete;
  MetricsAggregator& operator=(const MetricsAggregator&) = delete;
  MetricsAggregator& operator=(MetricsAggregator&&) = delete;

  explicit MetricsAggregator(const PerformanceCounterMath&);

  void Push(const FramePerformanceCounters&);
  [[nodiscard]] std::optional<FrameMetrics> Flush();

  void Reset() {
    auto pcm = mPerformanceCounterMath;
    this->~MetricsAggregator();
    new (this) MetricsAggregator(mPerformanceCounterMath);
  }

 private:
  const PerformanceCounterMath mPerformanceCounterMath;

  FrameMetrics mAccumulator {};
  LARGE_INTEGER mPreviousFrameEndTime {};
  bool mHavePartialData = false;
};