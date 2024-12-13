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
    duration *= (MicrosPerSecond / mMicrosGCD);
    duration /= (mResolution.QuadPart / mMicrosGCD);
    return std::chrono::microseconds {duration};
  }

  [[nodiscard]]
  inline std::chrono::microseconds ToDuration(
    const LARGE_INTEGER begin,
    const LARGE_INTEGER end) const {
    if (end.QuadPart < begin.QuadPart) {
      throw std::invalid_argument("end must be greater than begin");
    }
    return ToDuration({.QuadPart = end.QuadPart - begin.QuadPart});
  }

  /** Get an instance that is only valid for data collected on this system,
   * since the last reboot.
   */
  [[nodiscard]]
  static PerformanceCounterMath CreateForLiveData();

 private:
  static constexpr int64_t MicrosPerSecond = 1000 * 1000;
  LARGE_INTEGER mResolution {};
  int64_t mMicrosGCD {};
};