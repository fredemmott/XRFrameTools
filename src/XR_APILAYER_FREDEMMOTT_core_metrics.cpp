// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT

// clang-format off
#include <Windows.h>
// clang-format on

#include <openxr/openxr.h>
#include <openxr/openxr_loader_negotiation.h>

#include <atomic>
#include <format>
#include <ranges>

#include "BinaryLogWriter.hpp"
#include "Config.hpp"
#include "FrameMetricsStore.hpp"
#include "SHMWriter.hpp"
#include "Win32Utils.hpp"

#define HOOKED_OPENXR_FUNCS(X) \
  X(WaitFrame) \
  X(BeginFrame) \
  X(EndFrame)

static auto gConfig = Config::GetForOpenXRAPILayer();

static SHMWriter gSHM;
static std::optional<BinaryLogWriter> gBinaryLogger;
static FrameMetricsStore gFrameMetrics;

static void LogFrame(const FramePerformanceCounters& frame) {
  gSHM.LogFrame(frame);

  if (!gConfig.IsBinaryLoggingEnabled()) {
    if (gBinaryLogger) {
      dprint("tearing down binary logger");
      gBinaryLogger = std::nullopt;
    }
    return;
  }

  if (!gBinaryLogger) {
    dprint("creating binary logger");
    gBinaryLogger.emplace();
  }

  gBinaryLogger->LogFrame(frame);
}

PFN_xrWaitFrame next_xrWaitFrame {nullptr};
XrResult hooked_xrWaitFrame(
  XrSession session,
  const XrFrameWaitInfo* frameWaitInfo,
  XrFrameState* frameState) noexcept {
  auto& frame = gFrameMetrics.GetForWaitFrame();

  QueryPerformanceCounter(&frame.mWaitFrameStart);
  const auto ret = next_xrWaitFrame(session, frameWaitInfo, frameState);
  QueryPerformanceCounter(&frame.mWaitFrameStop);

  if (XR_FAILED(ret)) [[unlikely]] {
    frame.Reset();
    return ret;
  }

  frame.mDisplayTime = frameState->predictedDisplayTime;
  frame.mCanBegin.store(true);
  return ret;
}

PFN_xrBeginFrame next_xrBeginFrame {nullptr};
XrResult hooked_xrBeginFrame(
  XrSession session,
  const XrFrameBeginInfo* frameBeginInfo) noexcept {
  auto& frame = gFrameMetrics.GetForBeginFrame();

  QueryPerformanceCounter(&frame.mBeginFrameStart);
  const auto ret = next_xrBeginFrame(session, frameBeginInfo);
  QueryPerformanceCounter(&frame.mBeginFrameStop);

  if (XR_FAILED(ret)) [[unlikely]] {
    frame.Reset();
  }

  return ret;
}

PFN_xrEndFrame next_xrEndFrame {nullptr};
XrResult hooked_xrEndFrame(
  XrSession session,
  const XrFrameEndInfo* frameEndInfo) noexcept {
  auto& frame = gFrameMetrics.GetForEndFrame(frameEndInfo->displayTime);
  QueryPerformanceCounter(&frame.mEndFrameStart);
  const auto ret = next_xrEndFrame(session, frameEndInfo);
  QueryPerformanceCounter(&frame.mEndFrameStop);

  if (XR_SUCCEEDED(ret)) [[likely]] {
    LogFrame(frame);
  }

  frame.Reset();
  return ret;
}

#include "APILayerEntrypoints.inc.cpp"