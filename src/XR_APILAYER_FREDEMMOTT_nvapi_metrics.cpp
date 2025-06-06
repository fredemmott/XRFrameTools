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
using EncoderInfo = FramePerformanceCounters::EncoderInfo;

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
  EncoderInfo mEncoderInfo {};
};

std::deque<FrameData> gFrames;
std::mutex gFramesMutex;

void EnqueueFrameData(
  uint64_t displayTime,
  const GpuPerformanceInfo& gpu,
  const EncoderInfo& encoder) {
  std::unique_lock lock(gFramesMutex);
  if (gFrames.size() >= 10) {
    dprint("nvapi_metrics: too many frames enqueued");
    gFrames.pop_front();
  }
  gFrames.emplace_back(displayTime, gpu, encoder);
}

ApiLayerApi::LogFrameHookResult LoggingHook(Frame* frame) {
  std::unique_lock lock(gFramesMutex);
  auto it = std::ranges::find(
    gFrames, frame->mCore.mXrDisplayTime, &FrameData::mDisplayTime);
  if (it != gFrames.end()) {
    frame->mGpuPerformanceInformation = it->mGpuPerformanceInfo;
    frame->mValidDataBits |= FramePerformanceCounters::ValidDataBits::NVAPI;
    if (it->mEncoderInfo.mSessionCount > 0) {
      frame->mEncoders = it->mEncoderInfo;
      frame->mValidDataBits |= FramePerformanceCounters::ValidDataBits::NVEnc;
    }
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
  if (!gPhysicalGpuHandle) {
    return next_xrEndFrame(session, frameEndInfo);
  }

  NvU32 perfDecrease {};
  NV_GPU_PERF_PSTATE_ID pstate {};
  NV_GPU_CLOCK_FREQUENCIES frequencies {
    .version = NV_GPU_CLOCK_FREQUENCIES_VER,
  };
  GpuPerformanceInfo retGpu {};
  if (
    NvAPI_GPU_GetPerfDecreaseInfo(gPhysicalGpuHandle.value(), &perfDecrease)
      == NVAPI_OK
    && NvAPI_GPU_GetCurrentPstate(gPhysicalGpuHandle.value(), &pstate)
      == NVAPI_OK
    && NvAPI_GPU_GetAllClockFrequencies(
         gPhysicalGpuHandle.value(), &frequencies)
      == NVAPI_OK) {
    retGpu = {
      .mDecreaseReasons = perfDecrease,
      .mPState = static_cast<uint32_t>(pstate),
      .mGraphicsKHz = static_cast<uint32_t>(
        frequencies.domain[NVAPI_GPU_PUBLIC_CLOCK_GRAPHICS].frequency),
      .mMemoryKHz = static_cast<uint32_t>(
        frequencies.domain[NVAPI_GPU_PUBLIC_CLOCK_MEMORY].frequency),
    };
  }

  std::array<
    NV_ENCODER_PER_SESSION_INFO_V1,
    NV_ENCODER_SESSION_INFO_MAX_ENTRIES_V1>
    encoderSessions {};
  NV_ENCODER_SESSIONS_INFO encoderInfo {
    .version = NV_ENCODER_SESSIONS_INFO_VER,
    .pSessionInfo = encoderSessions.data(),
  };
  EncoderInfo retEncoder {};
  if (
    NvAPI_GPU_GetEncoderSessionsInfo(gPhysicalGpuHandle.value(), &encoderInfo)
    == NVAPI_OK) {
    retEncoder.mSessionCount = encoderInfo.sessionsCount;
    const auto sessionCount = std::min(
      static_cast<uint32_t>(encoderInfo.sessionsCount),
      static_cast<uint32_t>(retEncoder.mSessions.size()));
    for (uint32_t i = 0; i < sessionCount; ++i) {
      const auto& it = encoderSessions.at(i);
      // As of 2025-06-05, the NvAPI header says that `averageEncodeLatency`
      // is milliseconds, but it's actually microseconds
      //
      // https://github.com/NVIDIA/nvapi/issues/18
      retEncoder.mSessions.at(i) = {
        .mAverageFPS = it.averageEncodeFps,
        .mAverageLatency = it.averageEncodeLatency,
        .mProcessID = it.processId,
      };
    }
  }

  EnqueueFrameData(frameEndInfo->displayTime, retGpu, retEncoder);

  return next_xrEndFrame(session, frameEndInfo);
}

#define HOOKED_OPENXR_FUNCS(X) X(EndFrame)

#include "APILayerEntrypoints.inc.cpp"
