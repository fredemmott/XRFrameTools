// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT

// clang-format off
#include <Windows.h>
#include <TraceLoggingProvider.h>
// clang-format on

#include <nvapi.h>
#include <openxr/openxr.h>
#include <openxr/openxr_loader_negotiation.h>

#include <atomic>

#include "Win32Utils.hpp"

#define APILAYER_API __declspec(dllimport)

#include <mutex>
#include <queue>
#include <span>

#include "ApiLayerApi.hpp"

/* PS>
 * [System.Diagnostics.Tracing.EventSource]::new("XRFrameTools.nvapi_metrics")
 * 58a2bcbf-57ab-5ec3-d229-a5d23f055d5a
 */
TRACELOGGING_DEFINE_PROVIDER(
  gTraceProvider,
  "XRFrameTools.nvapi_metrics",
  (0x58a2bcbf, 0x57ab, 0x5ec3, 0xd2, 0x29, 0xa5, 0xd2, 0x3f, 0x05, 0x5d, 0x5a));

using GpuPerformanceInfo = FramePerformanceCounters::GpuPerformanceInfo;

namespace {
std::atomic_flag gHooked;
std::optional<NvPhysicalGpuHandle> gPhysicalGpuHandle;

ApiLayerApi::LogFrameHookResult LoggingHook(Frame* frame);

void InstallHook() {
  if (gHooked.test_and_set()) {
    return;
  }

  const auto api = ApiLayerApi::Get("nvapi_metrics");
  if (!api) {
    return;
  }
  api->AppendLogFrameHook(&LoggingHook);
  const auto activeLuid = api->GetActiveGpu();
  if (!activeLuid.has_value()) {
    dprint("nvapi_metrics: active GPU LUID is not available");
    return;
  }

  NvU32 logicalGpuCount {};
  NvLogicalGpuHandle logicalGpus[NVAPI_MAX_LOGICAL_GPUS];
  if (const auto ret = NvAPI_EnumLogicalGPUs(logicalGpus, &logicalGpuCount);
      ret != NVAPI_OK) {
    if (ret == NVAPI_NVIDIA_DEVICE_NOT_FOUND) {
      dprint("nvapi_metrics: no NVIDIA GPUs found");
      return;
    }
    dprint(
      "nvapi_metrics: NvAPI_EnumLogicalGPUs failed: {}",
      std::to_underlying(ret));
    return;
  }

  for (auto&& logical: std::span {logicalGpus, logicalGpuCount}) {
    LUID luid {};
    NV_LOGICAL_GPU_DATA data {
      .version = NV_LOGICAL_GPU_DATA_VER,
      .pOSAdapterId = &luid,
    };
    if (const auto ret = NvAPI_GPU_GetLogicalGpuInfo(logical, &data);
        ret != NVAPI_OK) {
      dprint(
        "nvapi_metrics: failed to retrieve info on a logical GPU: {}",
        std::to_underlying(ret));
      continue;
    }
    dprint(
      "nvapi_metrics: found physical NVIDIA GPU with LUID {:#018x}",
      std::bit_cast<uint64_t>(luid));
    if (
      std::bit_cast<uint64_t>(luid)
      == std::bit_cast<uint64_t>(activeLuid.value())) {
      if (data.physicalGpuCount > 0) {
        gPhysicalGpuHandle.emplace(data.physicalGpuHandles[0]);
        dprint("nvapi_metrics: found physical GPU handle matching active LUID");
      } else {
        dprint(
          "nvapi_metrics: found matching LUID, but no corresponding physical "
          "GPU");
      }
      break;
    }
  }
}

struct FrameData {
  uint64_t mDisplayTime {};
  GpuPerformanceInfo mGpuPerformanceInfo {};
};

std::deque<FrameData> gFrames;
std::mutex gFramesMutex;

void EnqueueFrameData(uint64_t displayTime, const GpuPerformanceInfo& data) {
  std::unique_lock lock(gFramesMutex);
  if (gFrames.size() >= 10) {
    dprint("nvapi_metrics: too many frames enqueued");
    gFrames.pop_front();
  }
  gFrames.emplace_back(displayTime, data);
}

ApiLayerApi::LogFrameHookResult LoggingHook(Frame* frame) {
  std::unique_lock lock(gFramesMutex);
  auto it = std::ranges::find(
    gFrames, frame->mXrDisplayTime, &FrameData::mDisplayTime);
  if (it != gFrames.end()) {
    frame->mGpuPerformanceInformation = it->mGpuPerformanceInfo;
    frame->mValidDataBits
      |= static_cast<uint64_t>(FramePerformanceCounters::ValidDataBits::NVAPI);
    gFrames.erase(it);
  }
  return ApiLayerApi::LogFrameHookResult::Ready;
}

}// namespace

PFN_xrEndFrame next_xrEndFrame {nullptr};
XrResult hooked_xrEndFrame(
  XrSession session,
  const XrFrameEndInfo* frameEndInfo) noexcept {
  InstallHook();
  if (gPhysicalGpuHandle) {
    NvU32 perfDecrease {};
    NV_GPU_PERF_PSTATE_ID pstate {};
    if (
      NvAPI_GPU_GetPerfDecreaseInfo(gPhysicalGpuHandle.value(), &perfDecrease)
        == NVAPI_OK
      && NvAPI_GPU_GetCurrentPstate(gPhysicalGpuHandle.value(), &pstate)
        == NVAPI_OK) {
      EnqueueFrameData(
        frameEndInfo->displayTime,
        GpuPerformanceInfo {
          .mDecreaseReasons = perfDecrease,
          .mPState = static_cast<uint32_t>(pstate),
        });
    }
  }
  return next_xrEndFrame(session, frameEndInfo);
}

#define HOOKED_OPENXR_FUNCS(X) X(EndFrame)

#include "APILayerEntrypoints.inc.cpp"