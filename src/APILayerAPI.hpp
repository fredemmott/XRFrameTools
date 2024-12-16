// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT
#pragma once

#include <XRFrameTools/ABIKey.hpp>

// This file contains the API used for OpenXR API layers to communicate with
// each other.

struct FrameMetricsStore;
struct APILayerAPI {
  FrameMetricsStore* GetFrameMetricsStore();
};

/// MAY RETURN nullptr
extern "C" APILAYER_API APILayerAPI* XRFrameTools_GetAPILayerAPI(
  const char* abiKey = ABIKey,
  std::size_t abiKeyLength = sizeof(ABIKey));
