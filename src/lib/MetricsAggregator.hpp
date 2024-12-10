// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT
#pragma once

#include <optional>

#include "FrameMetrics.hpp"
#include "FramePerformanceCounters.hpp"

struct AggregatedFrameMetrics : FrameMetrics {
  uint16_t mFrameCount {};
  std::chrono::microseconds mAppCpu;
  std::chrono::microseconds mSincePreviousFrame {};
};

class MetricsAggregator {
 public:
  void Push(const FramePerformanceCounters&);
  [[nodiscard]] std::optional<AggregatedFrameMetrics> Flush();

 private:
  AggregatedFrameMetrics mAccumulator {};
  LARGE_INTEGER mPreviousFrameEndTime {};
  bool mHavePartialData = false;
};