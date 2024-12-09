// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT
#pragma once

#include <wil/resource.h>

#include <array>
#include <chrono>

#include "FramePerformanceCounters.hpp"

struct SHM final {
  static constexpr auto MaxFrameCount = 128;
  alignas(16) LONGLONG mWriterCount {};
  LARGE_INTEGER mLastUpdate {};
  uint64_t mFrameCount {};
  DWORD mWriterProcessID {};

  std::array<FramePerformanceCounters, MaxFrameCount> mFrameMetrics;

  auto& GetFramePerformanceCounters(uint64_t index) const noexcept {
    return mFrameMetrics.at(index % SHM::MaxFrameCount);
  }

  std::chrono::microseconds GetAge() const noexcept;
};
// This can change, just check that 32-bit and 64-bit builds get the same value
static_assert(sizeof(SHM) == 6176);

class SHMClient {
  SHMClient(const SHMClient&) = delete;
  SHMClient(SHMClient&&) = delete;
  SHMClient& operator=(const SHMClient&) = delete;
  SHMClient& operator=(SHMClient&&) = delete;

 protected:
  SHMClient();
  ~SHMClient();

  SHM* MaybeGetSHM() const noexcept;

 private:
  wil::unique_hfile mMapping;
  wil::unique_any<void*, decltype(&::UnmapViewOfFile), &UnmapViewOfFile> mView;
};

class SHMWriter final : public SHMClient {
 public:
  SHMWriter();
  ~SHMWriter();

  void LogFrame(const FramePerformanceCounters& metrics) const;
};

class SHMReader final : public SHMClient {
 public:
  SHMReader();
  ~SHMReader();

  bool IsValid() const noexcept;

  // Throws std::logic_error if !IsValid()
  const SHM& GetSHM() const;

  inline auto operator->() const {
    return &GetSHM();
  }
};