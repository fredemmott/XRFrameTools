// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT
#include "PerformanceCounterMath.hpp"

PerformanceCounterMath::PerformanceCounterMath(LARGE_INTEGER frequency)
  : mResolution(frequency) {
  if (frequency.QuadPart == 0) {
    throw std::out_of_range("Frequency can not be 0");
  }
}

PerformanceCounterMath PerformanceCounterMath::CreateForLiveData() {
  LARGE_INTEGER pf {};
  QueryPerformanceFrequency(&pf);
  return {pf};
}