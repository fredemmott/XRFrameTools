// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT
#include "PerformanceCountersToDuration.hpp"

static LARGE_INTEGER GetPerformanceCounterFrequency() noexcept {
  LARGE_INTEGER ret;
  QueryPerformanceFrequency(&ret);
  return ret;
}

static const auto PerformanceCounterFrequency = GetPerformanceCounterFrequency();


std::chrono::microseconds PerformanceCountersToDuration(const LONGLONG diff) noexcept {
  auto duration = diff;
  duration *= 1000000;
  duration /= PerformanceCounterFrequency.QuadPart;
  return std::chrono::microseconds { duration };
}