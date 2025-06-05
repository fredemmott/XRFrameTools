// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT
#pragma once

#include <dxgi1_4.h>

#include <chrono>

struct FrameMetrics {
  uint16_t mFrameCount {};
  std::chrono::microseconds mSincePreviousFrame {};
  std::chrono::microseconds mSinceFirstFrame {};
  uint64_t mLastXrDisplayTime {};
  LARGE_INTEGER mLastEndFrameStop {};

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
  uint32_t mGpuPStateMin {};
  uint32_t mGpuPStateMax {};

  uint32_t mGpuGraphicsKHzMin {};
  uint32_t mGpuGraphicsKHzMax {};
  uint32_t mGpuMemoryKHzMin {};
  uint32_t mGpuMemoryKHzMax {};
};
