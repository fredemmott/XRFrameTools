// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT
#pragma once

#include <XRFrameTools/ABIKey.hpp>
#include <optional>

#include "FrameMetricsStore.hpp"
#include "Win32Utils.hpp"

// This file contains the API used for OpenXR API layers to communicate with
// each other.

class ApiLayerApi {
 public:
  ApiLayerApi() = default;
  virtual ~ApiLayerApi() = default;

  enum class LogFrameHookResult {
    Ready,
    Pending,
  };
  using LogFrameHook = LogFrameHookResult (*)(Frame*);
  virtual void AppendLogFrameHook(LogFrameHook logFrameHook) = 0;

  [[nodiscard]] std::optional<LUID> GetActiveGpu() const noexcept {
    return mActiveGpu;
  }

  void SetActiveGpu(const LUID& gpu) noexcept {
    mActiveGpu = gpu;
  }

  static ApiLayerApi* Get(std::string_view callerComponent);

 private:
  std::optional<LUID> mActiveGpu;
};

/// MAY RETURN nullptr
extern "C" APILAYER_API ApiLayerApi* XRFrameTools_GetApiLayerApi(
  const char* abiKey,
  std::size_t abiKeyLength);

constexpr auto CoreMetrics32Dll = L"XR_APILAYER_FREDEMMOTT_core_metrics32.dll";
constexpr auto CoreMetrics64Dll = L"XR_APILAYER_FREDEMMOTT_core_metrics64.dll";
constexpr auto CoreMetricsDll
  = (sizeof(void*) * 8 == 64) ? CoreMetrics64Dll : CoreMetrics32Dll;

inline ApiLayerApi* ApiLayerApi::Get(std::string_view callerComponent) {
  const auto coreMetrics = GetModuleHandleW(CoreMetricsDll);
  if (!coreMetrics) {
    dprint(
      "{}: couldn't find core_metrics: {:#010x}",
      callerComponent,
      static_cast<uint32_t>(HRESULT_FROM_WIN32(GetLastError())));
    return nullptr;
  }
  auto getter = GetProcAddress(coreMetrics, "XRFrameTools_GetApiLayerApi");
  if (!getter) {
    dprint("{}: couldn't find inter-layer API entrypoint", callerComponent);
    return nullptr;
  }
  auto invocable
    = reinterpret_cast<decltype(&XRFrameTools_GetApiLayerApi)>(getter);
  const auto api = invocable(ABIKey, sizeof(ABIKey));
  if (!api) {
    dprint(
      "{}: couldn't get an instance of the inter-layer API", callerComponent);
    return nullptr;
  }
  return api;
}
