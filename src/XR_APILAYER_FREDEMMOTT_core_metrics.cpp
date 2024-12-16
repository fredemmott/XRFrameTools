// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT

// clang-format off
#include <Windows.h>
// clang-format on

#include <openxr/openxr.h>
#include <openxr/openxr_loader_negotiation.h>

#include <atomic>
#include <format>

#include "BinaryLogWriter.hpp"
#include "Config.hpp"
#include "FrameMetricsStore.hpp"
#include "SHMWriter.hpp"
#include "Win32Utils.hpp"

#define APILAYER_API __declspec(dllexport)
#include <deque>

#include "APILayerAPI.hpp"

static auto gConfig = Config::GetForOpenXRAPILayer();

static SHMWriter gSHM;
static std::optional<BinaryLogWriter> gBinaryLogger;
static FrameMetricsStore gFrameMetrics;
static std::vector<APILayerAPI::LogFrameHook> gLoggingHooks;

static std::deque<Frame> gLogQueue;
static std::mutex gLogQueueMutex;

void AppendLogFrameHook(APILayerAPI::LogFrameHook hook) {
  gLoggingHooks.push_back(hook);
}

static APILayerAPI gAPILayerAPI {
  .AppendLogFrameHook = &AppendLogFrameHook,
};

APILayerAPI* XRFrameTools_GetAPILayerAPI(
  const char* abiKey,
  std::size_t abiKeyLength) {
  if (abiKeyLength != sizeof(ABIKey)) {
    return nullptr;
  }
  if (std::string_view {abiKey} != std::string_view {ABIKey}) {
    return nullptr;
  }

  return &gAPILayerAPI;
}

enum class LogFrameResult {
  Complete,
  Pending,
};

[[nodiscard]]
static LogFrameResult LogFrame(Frame& frame) {
  for (auto&& hook: gLoggingHooks) {
    if (hook(&frame) == APILayerAPI::LogFrameHookResult::Pending) {
      return LogFrameResult::Pending;
    }
  }
  gSHM.LogFrame(frame);

  if (!gConfig.IsBinaryLoggingEnabled()) {
    if (gBinaryLogger) {
      dprint("tearing down binary logger");
      gBinaryLogger = std::nullopt;
    }
    return LogFrameResult::Complete;
  }

  if (!gBinaryLogger) {
    dprint("creating binary logger");
    gBinaryLogger.emplace();
  }

  gBinaryLogger->LogFrame(frame);
  return LogFrameResult::Complete;
}

static void FlushMetrics() {
  if (gLogQueue.empty()) {
    return;
  }
  std::unique_lock<std::mutex> lock(gLogQueueMutex);
  while (!gLogQueue.empty()) {
    if (LogFrame(gLogQueue.front()) == LogFrameResult::Pending) {
      break;
    }
    gLogQueue.pop_front();
  }
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
  FlushMetrics();

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
    std::unique_lock lock(gLogQueueMutex);
    gLogQueue.push_back({static_cast<const Frame&>(frame)});
  }

  frame.Reset();
  return ret;
}

#define HOOKED_OPENXR_FUNCS(X) \
  X(WaitFrame) \
  X(BeginFrame) \
  X(EndFrame)

#include "APILayerEntrypoints.inc.cpp"