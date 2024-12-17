// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT
#pragma once

#include <wil/resource.h>

#include <array>

#include "FramePerformanceCounters.hpp"

struct SHM final {
  static constexpr auto MaxFrameCount = 128;
  alignas(16) LONGLONG mWriterCount {};
  LARGE_INTEGER mLastUpdate {};
  uint64_t mFrameCount {};
  DWORD mWriterProcessID {};

  std::array<FramePerformanceCounters, MaxFrameCount> mFrameMetrics;

  [[nodiscard]]
  auto& GetFramePerformanceCounters(const uint64_t index) const noexcept {
    return mFrameMetrics.at(index % SHM::MaxFrameCount);
  }
};
// This can change, just check that 32-bit and 64-bit builds get the same value
static_assert(sizeof(SHM) == 13344);
