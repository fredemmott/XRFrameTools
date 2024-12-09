// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT
#pragma once

#include <chrono>

struct FramePerformanceCounters;

struct FrameMetrics {
  FrameMetrics() = default;
  FrameMetrics(const FramePerformanceCounters&);

  // Computed by constructor
  std::chrono::microseconds mWait;
  std::chrono::microseconds mAppCpu;
  std::chrono::microseconds mRuntimeCpu;
  std::chrono::microseconds mTotalCpu;
};