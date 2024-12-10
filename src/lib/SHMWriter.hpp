// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT
#pragma once

#include "SHMClient.hpp"

struct FramePerformanceCounters;

class SHMWriter final : public SHMClient {
 public:
  SHMWriter();
  ~SHMWriter();

  void LogFrame(const FramePerformanceCounters& metrics) const;
};
