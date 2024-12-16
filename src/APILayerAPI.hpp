// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT
#pragma once

#include <XRFrameTools/ABIKey.hpp>
#include <functional>

#include "FrameMetricsStore.hpp"

// This file contains the API used for OpenXR API layers to communicate with
// each other.

struct APILayerAPI final {
  using LogFrameHook = void (*)(Frame&);
  using PFN_AppendLogFrameHook = void (*)(LogFrameHook);
  PFN_AppendLogFrameHook AppendLogFrameHook {nullptr};
};

/// MAY RETURN nullptr
extern "C" APILAYER_API APILayerAPI* XRFrameTools_GetAPILayerAPI(
  const char* abiKey = ABIKey,
  std::size_t abiKeyLength = sizeof(ABIKey));

constexpr auto CoreMetrics32Dll = L"XR_APILAYER_FREDEMMOTT_core_metrics32.dll";
constexpr auto CoreMetrics64Dll = L"XR_APILAYER_FREDEMMOTT_core_metrics64.dll";
constexpr auto CoreMetricsDll
  = (sizeof(void*) * 8 == 64) ? CoreMetrics64Dll : CoreMetrics32Dll;
