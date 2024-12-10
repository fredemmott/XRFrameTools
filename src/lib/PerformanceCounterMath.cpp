// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT
#include "PerformanceCounterMath.hpp"

static LARGE_INTEGER GetPerformanceFrequency() {
  LARGE_INTEGER ret {};
  QueryPerformanceFrequency(&ret);
  return ret;
}
static const auto gLiveFrequency = GetPerformanceFrequency();

PerformanceCounterMath::PerformanceCounterMath(LARGE_INTEGER frequency)
  : mResolution(frequency) {
}

PerformanceCounterMath PerformanceCounterMath::CreateForLiveData() {
  return {gLiveFrequency};
}