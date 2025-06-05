// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT
#pragma once

#include <Windows.h>
#include <wil/resource.h>

#include <BinaryLog.hpp>
#include <array>
#include <mutex>
#include <thread>
#include <unordered_set>

#include "FramePerformanceCounters.hpp"

class BinaryLogWriter {
 public:
  BinaryLogWriter();
  ~BinaryLogWriter();

  void LogFrame(const FramePerformanceCounters&);

 private:
  wil::unique_hfile mFile;

  BinaryLog::FileFooter mFooter {};

  static constexpr auto RingBufferSize = 128;
  std::array<FramePerformanceCounters, RingBufferSize> mRingBuffer;

  std::mutex mProducedMutex;
  uint64_t mProduced {};
  uint64_t mConsumed {};

  wil::unique_handle mWakeEvent {CreateEventW(nullptr, FALSE, FALSE, nullptr)};
  std::jthread mThread;

  std::unordered_set<DWORD> mLoggedProcesses;

  void OpenFile();
  void Run(std::stop_token);
  uint64_t GetProduced();
  void LogProcess(DWORD pid);
  void WriteFooter();
};