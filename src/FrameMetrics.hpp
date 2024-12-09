// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT
#pragma once

#include <Windows.h>

struct FrameMetrics {
  LARGE_INTEGER mWaitFrameStart {};
  LARGE_INTEGER mWaitFrameStop {};
  LARGE_INTEGER mBeginFrameStart {};
  LARGE_INTEGER mBeginFrameStop {};
  LARGE_INTEGER mEndFrameStart {};
  LARGE_INTEGER mEndFrameStop {};
};
// Increase this if you add additional members; this assertion is here to make
// sure the struct is the same size in 32-bit and 64-bit builds
static_assert(sizeof(FrameMetrics) == 48);