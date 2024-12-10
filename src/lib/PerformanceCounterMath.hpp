// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT
#pragma once

#include <Windows.h>

#include <chrono>

class PerformanceCounterMath {
 public:
  PerformanceCounterMath() = delete;
  /** Create an instance for a known QueryPerformanceFrequency()
   *
   * @see PerformanceCounters::ForLiveData()
   */
  PerformanceCounterMath(LARGE_INTEGER frequency);

  [[nodiscard]] inline LARGE_INTEGER GetResolution() const noexcept {
    return mResolution;
  }

  [[nodiscard]]
  inline std::chrono::microseconds ToDuration(
    const LARGE_INTEGER diff) const noexcept {
    auto duration = diff.QuadPart;
    duration *= 1000000;
    duration /= mResolution.QuadPart;
    return std::chrono::microseconds {duration};
  }

  [[nodiscard]]
  inline std::chrono::microseconds ToDuration(
    const LARGE_INTEGER begin,
    const LARGE_INTEGER end) const noexcept {
    return ToDuration({.QuadPart = end.QuadPart - begin.QuadPart});
  }

  /** Get an instance that is only valid for data collected on this system,
   * since the last reboot.
   */
  [[nodiscard]]
  static PerformanceCounterMath CreateForLiveData();

 private:
  LARGE_INTEGER mResolution {};
};