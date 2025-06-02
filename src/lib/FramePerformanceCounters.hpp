// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT
#pragma once

#include <Windows.h>
#include <dxgi1_4.h>

// All values are from QueryPerformanceCounters
struct FramePerformanceCounters {
  // Used for BinLog
  static constexpr auto Version = "2025-06-02#02";

  enum class ValidDataBits : uint64_t {
    D3D11 = 1,
    NVAPI = 2,
  };

  uint64_t mValidDataBits {};

  // core_metrics - QueryPerformanceCounter()
  uint64_t mXrDisplayTime {};

  LARGE_INTEGER mWaitFrameStart {};
  LARGE_INTEGER mWaitFrameStop {};
  LARGE_INTEGER mBeginFrameStart {};
  LARGE_INTEGER mBeginFrameStop {};
  LARGE_INTEGER mEndFrameStart {};
  LARGE_INTEGER mEndFrameStop {};

  // d3d11_metrics
  uint64_t mRenderGpu {};// microseconds
  DXGI_QUERY_VIDEO_MEMORY_INFO mVideoMemoryInfo {};

  // Currently only valid if (mValidData & NVAPI)
  struct GpuPerformanceInfo {
    uint32_t mDecreaseReasons {};// NVAPI_GPU_PERF_DECREASE bitmask
    uint32_t mPState {};// NVAPI_GPU_PSTATE_ID
    uint32_t mGraphicsKHz {};// NVAPI_GPU_PUBLIC_CLOCK_GRAPHICS
    uint32_t mMemoryKHz {};// NVAPI_GPU_PUBLIC_CLOCK_MEMORY
  } mGpuPerformanceInformation {};
};

// Increase this if you add additional members; this assertion is here to make
// sure the struct is the same size in 32-bit and 64-bit builds
static_assert(sizeof(FramePerformanceCounters) == 120);
