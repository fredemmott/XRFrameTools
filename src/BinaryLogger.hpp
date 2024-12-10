// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT
#pragma once

#include <Windows.h>
#include <wil/resource.h>

#include <array>
#include <chrono>
#include <mutex>
#include <thread>

#include "FramePerformanceCounters.hpp"

class BinaryLogger {
 public:
  static constexpr auto Version = "2024091201";

  BinaryLogger();
  ~BinaryLogger();

  void LogFrame(const FramePerformanceCounters&);

 private:
  wil::unique_hfile mFile;

  static constexpr auto RingBufferSize = 128;
  std::array<FramePerformanceCounters, RingBufferSize> mRingBuffer;

  std::mutex mProducedMutex;
  uint64_t mProduced {};
  uint64_t mConsumed {};

  wil::unique_handle mWakeEvent {CreateEventW(nullptr, FALSE, FALSE, nullptr)};
  std::jthread mThread;

  std::vector<FramePerformanceCounters> mPending;
  std::chrono::steady_clock::time_point mLastFlush;

  void OpenFile();
  void FlushToDisk();
  void Run(std::stop_token);
  uint64_t GetProduced();
};