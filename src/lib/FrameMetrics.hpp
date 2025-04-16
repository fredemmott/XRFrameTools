// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT
#pragma once

#include <dxgi1_4.h>

#include <chrono>

struct FrameMetrics {
  uint16_t mFrameCount {};
  std::chrono::microseconds mSincePreviousFrame {};
  uint64_t mLastXrDisplayTime {};

  uint64_t mValidDataBits {};

  // Together these are 'runtime CPU'
  std::chrono::microseconds mWaitFrameCpu {};
  std::chrono::microseconds mBeginFrameCpu {};
  std::chrono::microseconds mEndFrameCpu {};// A.K.A. 'Submit CPU'

  std::chrono::microseconds mRenderCpu {};
  std::chrono::microseconds mAppCpu {};

  std::chrono::microseconds mRenderGpu {};

  DXGI_QUERY_VIDEO_MEMORY_INFO mVideoMemoryInfo {};

  uint32_t mGpuPerformanceDecreaseReasons {};
  uint32_t mGpuLowestPState {};
  uint32_t mGpuHighestPState {};
};