// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT
#pragma once

#include <Windows.h>
#include <dxgi1_4.h>

#include <array>

struct FramePerformanceCounters {
  // Used for BinLog
  static constexpr auto Version = "2025-06-05#01";

  enum class ValidDataBits : uint64_t {
    GpuTime = 1 << 0,
    VRAM = 1 << 1,
    NVAPI = 1 << 2,
    NVEnc = 1 << 3,
  };

  uint64_t mValidDataBits {};

  struct Core {
    uint64_t mXrDisplayTime {};

    // All values are from QueryPerformanceCounters
    LARGE_INTEGER mWaitFrameStart {};
    LARGE_INTEGER mWaitFrameStop {};
    LARGE_INTEGER mBeginFrameStart {};
    LARGE_INTEGER mBeginFrameStop {};
    LARGE_INTEGER mEndFrameStart {};
    LARGE_INTEGER mEndFrameStop {};
  } mCore;

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

  struct EncoderInfo {
    struct Session {
      uint32_t mAverageFPS {};
      uint32_t mAverageLatency {};
      uint32_t mProcessID {};
      uint32_t mReserved {/* padding for 32-bit builds */};
    };
    std::array<Session, 4> mSessions {};
    uint32_t mSessionCount {};
  } mEncoders;
};

// Increase this if you add additional members; this assertion is here to make
// sure the struct is the same size in 32-bit and 64-bit builds
static_assert(sizeof(FramePerformanceCounters) == 192);

constexpr uint64_t& operator|=(
  uint64_t& lhs,
  const FramePerformanceCounters::ValidDataBits rhs) {
  lhs |= std::to_underlying(rhs);
  return lhs;
}

constexpr auto operator&(
  const uint64_t lhs,
  const FramePerformanceCounters::ValidDataBits rhs) {
  return static_cast<FramePerformanceCounters::ValidDataBits>(
    lhs & std::to_underlying(rhs));
}