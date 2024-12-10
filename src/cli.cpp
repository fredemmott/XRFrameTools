// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT

// clang-format off
#include <Windows.h>
// clang-format on

#include <cstdio>
#include <filesystem>
#include <print>
#include <thread>

#include "FrameMetrics.hpp"
#include "MetricsAggregator.hpp"
#include "PerformanceCountersToDuration.hpp"
#include "SHM.hpp"

static void PrintFrame(const AggregatedFrameMetrics& afm) {
  std::println(
    "Wait\t{}\tApp\t{}\tRuntime\t{}\tCPU\t{}\tInterval\t{}\tFPS\t{:0.1f}",
    afm.mWait,
    afm.mAppCpu,
    afm.mRuntimeCpu,
    afm.mTotalCpu,
    afm.mSincePreviousFrame,
    1000000.0f / afm.mSincePreviousFrame.count());
}

int main(int argc, char** argv) {
  SHMReader shm;
  if (!shm.IsValid()) {
    std::println(
      stderr, "Failed to open shared memory segment - permissions error?");
    return EXIT_FAILURE;
  }

  if (shm->GetAge() > std::chrono::seconds(1)) {
    std::println(stderr, "Waiting for data...");
    while (shm->GetAge() > std::chrono::seconds(1)) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  }

  if (wil::unique_handle writer {OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION, FALSE, shm->mWriterProcessID)}) {
    char buffer[MAX_PATH];
    DWORD length {sizeof(buffer)};
    if (
      QueryFullProcessImageNameA(writer.get(), 0, buffer, &length) && length) {
      std::string_view path {buffer};
      while (!path.back()) {
        path.remove_suffix(1);
      }
      std::println(
        stderr,
        "OpenXR app: {}",
        std::filesystem::canonical(std::filesystem::path {path}).string());
    }
  }

  constexpr auto OutputRatio = 10;
  std::println(stderr, "Showing batches of {} frames", OutputRatio);

  constexpr auto PollRate = 5;
  constexpr auto PollInterval = std::chrono::milliseconds(1000) / PollRate;

  uint64_t frameCount = shm->mFrameCount;
  MetricsAggregator aggregator;
  while (shm->GetAge() < std::chrono::seconds(1)) {
    const auto begin = std::chrono::steady_clock::now();
    if (frameCount > shm->mFrameCount) {
      // Deal with process changes
      frameCount = shm->mFrameCount;
    }
    while (frameCount < shm->mFrameCount) {
      const auto index = frameCount++;
      const auto& frame = shm->GetFramePerformanceCounters(index);
      aggregator.Push(frame);

      if (index % OutputRatio == 0) {
        const auto metrics = aggregator.Flush();
        if (metrics) {
          PrintFrame(*metrics);
        }
      }
    }

    const auto end = std::chrono::steady_clock::now();
    const auto elapsed = end - begin;
    if (elapsed < PollInterval) {
      std::this_thread::sleep_for(PollInterval - elapsed);
    }
  }
  std::println(stderr, "Data source went away");

  return 0;
}