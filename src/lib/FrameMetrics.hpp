// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT
#pragma once

#include <dxgi1_4.h>

#include <chrono>

struct PerformanceCounterMath;
struct FramePerformanceCounters;

struct FrameMetrics {
  FrameMetrics() = default;
  FrameMetrics(const PerformanceCounterMath&, const FramePerformanceCounters&);

  // Computed by constructor
  std::chrono::microseconds mWaitCpu {};
  std::chrono::microseconds mRenderCpu {};
  std::chrono::microseconds mRenderGpu {};
  std::chrono::microseconds mRuntimeCpu {};

  DXGI_QUERY_VIDEO_MEMORY_INFO mVideoMemoryInfo {};
};