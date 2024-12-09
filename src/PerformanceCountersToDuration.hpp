// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT
#pragma once

#include <Windows.h>

#include <chrono>

std::chrono::microseconds PerformanceCountersToDuration(LONGLONG diff) noexcept;