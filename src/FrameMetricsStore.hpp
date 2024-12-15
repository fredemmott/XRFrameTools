// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT
#pragma once
#include <atomic>
#include <array>

#include "FramePerformanceCounters.hpp"

struct Frame final : FramePerformanceCounters {
  Frame() = default;
  ~Frame() = default;

  Frame(const Frame&) = delete;
  Frame(Frame&&) = delete;
  Frame& operator=(const Frame&) = delete;
  Frame& operator=(Frame&&) = delete;

  uint64_t mDisplayTime {};
  std::atomic<bool> mCanBegin {};

  // Don't want to accidentally move/copy this, but do want to be able to reset
  // it back to the initial state
  void Reset() {
    this->~Frame();
    new (this) Frame();
  }
};

class FrameMetricsStore {
 public:
  Frame& GetForWaitFrame() noexcept;
  Frame& GetForBeginFrame() noexcept;
  Frame& GetForEndFrame(uint64_t displayTime) noexcept;

 private:
  std::array<Frame, 3> mTrackedFrames;
  std::array<Frame, 3> mUntrackedFrames;
  std::atomic_uint64_t mWaitFrameCount;
  std::atomic_uint64_t mUntrackedFrameCount;
};
