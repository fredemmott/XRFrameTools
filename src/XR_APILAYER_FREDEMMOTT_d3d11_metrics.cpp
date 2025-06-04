// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT

// clang-format off
#include <Windows.h>
#include <TraceLoggingProvider.h>
// clang-format on

#define XR_USE_GRAPHICS_API_D3D11
#define APILAYER_API __declspec(dllimport)

#include <d3d11.h>
#include <openxr/openxr.h>
#include <openxr/openxr_loader_negotiation.h>
#include <openxr/openxr_platform.h>
#include <wil/com.h>

#include <atomic>
#include <expected>
#include <format>
#include <mutex>
#include <numeric>
#include <span>

#include "ApiLayerApi.hpp"
#include "CheckHResult.hpp"
#include "D3D11GpuTimer.hpp"
#include "FrameMetricsStore.hpp"
#include "Win32Utils.hpp"

/* PS>
 * [System.Diagnostics.Tracing.EventSource]::new("XRFrameTools.d3d11_metrics")
 * aa04a9b2-6963-5059-17d9-6e98dbfa3053
 */
TRACELOGGING_DEFINE_PROVIDER(
  gTraceProvider,
  "XRFrameTools.d3d11_metrics",
  (0xaa04a9b2, 0x6963, 0x5059, 0x17, 0xd9, 0x6e, 0x98, 0xdb, 0xfa, 0x30, 0x53));

namespace {

struct D3D11Frame {
  D3D11Frame() = delete;
  explicit D3D11Frame(ID3D11Device* device) : mGpuTimer(device) {
    wil::com_ptr<IDXGIDevice> dxgiDevice;
    CheckHResult(device->QueryInterface(dxgiDevice.put()));
    wil::com_ptr<IDXGIAdapter> dxgiAdapter;
    CheckHResult(dxgiDevice->GetAdapter(dxgiAdapter.put()));
    CheckHResult(dxgiAdapter->QueryInterface(mAdapter.put()));
  }

  void StartRender(uint64_t predictedDisplayTime) {
    mPredictedDisplayTime = predictedDisplayTime;
    mDisplayTime = {};
    mVideoMemoryInfo = {};
    mGpuTimer.Start();
  }

  void StopRender(uint64_t displayTime) {
    mDisplayTime = displayTime;
#ifndef NDEBUG
    if (mDisplayTime != mPredictedDisplayTime) {
      dprint("Display time mismatch");
      __debugbreak();
    }
#endif
    mGpuTimer.Stop();

    const auto qvmiResult = mAdapter->QueryVideoMemoryInfo(
      0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &mVideoMemoryInfo);
    if (FAILED(qvmiResult)) {
      TraceLoggingWrite(
        gTraceProvider,
        "QueryVideoMemoryInfo/Failure",
        TraceLoggingValue(qvmiResult, "HRESULT"),
        TraceLoggingValue(mDisplayTime, "DisplayTime"));
    }
  }

  void GetVideoMemoryInfo(DXGI_QUERY_VIDEO_MEMORY_INFO& it) {
    it = mVideoMemoryInfo;
  }

  uint64_t GetPredictedDisplayTime() const noexcept {
    return mPredictedDisplayTime;
  }

  uint64_t GetDisplayTime() const noexcept {
    return mDisplayTime;
  }

  std::expected<uint64_t, GpuDataError> GetRenderMicroseconds() {
    const auto ret = mGpuTimer.GetMicroseconds();

    if (ret == std::unexpected {GpuDataError::Pending}) {
      return ret;
    }

    mPredictedDisplayTime = {};
    mDisplayTime = {};

    return ret;
  }

 private:
  wil::com_ptr<IDXGIAdapter3> mAdapter;
  uint64_t mPredictedDisplayTime {};
  uint64_t mDisplayTime {};

  DXGI_QUERY_VIDEO_MEMORY_INFO mVideoMemoryInfo {};
  D3D11GpuTimer mGpuTimer;
};

ID3D11Device* gDevice {nullptr};
std::mutex gFramesMutex;
std::vector<D3D11Frame> gFrames;
uint64_t gBeginFrameCounter {0};
std::uint64_t gWaitedDisplayTime = {};

std::atomic_flag gHooked;

bool gIsEnabled {false};

}// namespace

PFN_xrBeginFrame next_xrBeginFrame {nullptr};
XrResult hooked_xrBeginFrame(
  XrSession session,
  const XrFrameBeginInfo* frameBeginInfo) noexcept {
  const auto ret = next_xrBeginFrame(session, frameBeginInfo);
  if (!gIsEnabled) {
    return ret;
  }

  if (!XR_SUCCEEDED(ret)) {
    return ret;
  }

  if (const auto time = std::exchange(gWaitedDisplayTime, 0)) {
    std::unique_lock lock(gFramesMutex);
    auto it
      = std::ranges::find(gFrames, 0, &D3D11Frame::GetPredictedDisplayTime);
    if (it == gFrames.end()) {
      if (gFrames.size() > 10) {
        dprint("Runaway D3D11 frame timer pool size");
        return ret;
      }
      gFrames.emplace_back(gDevice);
      dprint("Increased D3D11 timer pool size to {}", gFrames.size());
      it = gFrames.end() - 1;
    }
    it->StartRender(time);
  }

  return ret;
}

PFN_xrEndFrame next_xrEndFrame {nullptr};
XrResult hooked_xrEndFrame(
  XrSession session,
  const XrFrameEndInfo* frameEndInfo) noexcept {
  if (!gIsEnabled) {
    return next_xrEndFrame(session, frameEndInfo);
  }

  {
    std::unique_lock lock(gFramesMutex);
    auto it = std::ranges::find(
      gFrames, frameEndInfo->displayTime, &D3D11Frame::GetPredictedDisplayTime);
    if (it != gFrames.end()) {
      it->StopRender(frameEndInfo->displayTime);
    } else {
      TraceLoggingWrite(
        gTraceProvider,
        "xrEndFrame/MissingBeginFrame",
        TraceLoggingValue(frameEndInfo->displayTime, "DisplayTime"));
    }
  }
  return next_xrEndFrame(session, frameEndInfo);
}

static ApiLayerApi::LogFrameHookResult LoggingHook(Frame* frame) {
  using Result = ApiLayerApi::LogFrameHookResult;
  if (!gIsEnabled) {
    return Result::Ready;
  }

  if (frame->mRenderGpu) {
    return Result::Ready;
  }

  std::unique_lock lock(gFramesMutex);
  auto it = std::ranges::find(
    gFrames, frame->mCore.mXrDisplayTime, &D3D11Frame::GetDisplayTime);
  if (it == gFrames.end()) {
    return Result::Ready;
  }

  const auto timer = it->GetRenderMicroseconds();
  if (timer.has_value()) {
    frame->mRenderGpu = timer.value();
    it->GetVideoMemoryInfo(frame->mVideoMemoryInfo);

    frame->mValidDataBits
      |= std::to_underlying(FramePerformanceCounters::ValidDataBits::D3D11);
    return Result::Ready;
  }
  switch (timer.error()) {
    case GpuDataError::Pending:
      return Result::Pending;
    case GpuDataError::Unusable:
      return Result::Ready;
  }
  return Result::Ready;
}

PFN_xrCreateSession next_xrCreateSession {nullptr};
XrResult hooked_xrCreateSession(
  XrInstance instance,
  const XrSessionCreateInfo* createInfo,
  XrSession* session) {
  gIsEnabled = false;
  const auto ret = next_xrCreateSession(instance, createInfo, session);
  if (XR_FAILED(ret)) {
    return ret;
  }

  for (auto it = static_cast<const XrBaseInStructure*>(createInfo->next); it;
       it = static_cast<const XrBaseInStructure*>(it->next)) {
    if (it->type != XR_TYPE_GRAPHICS_BINDING_D3D11_KHR) {
      continue;
    }

    dprint("d3d11_metrics: session created");

    auto graphicsBinding
      = reinterpret_cast<const XrGraphicsBindingD3D11KHR*>(it);
    gFrames.clear();
    gDevice = graphicsBinding->device;
    wil::com_ptr<IDXGIDevice> dxgiDevice;
    gDevice->QueryInterface(dxgiDevice.put());
    wil::com_ptr<IDXGIAdapter> dxgiAdapter;
    dxgiDevice->GetAdapter(dxgiAdapter.put());
    DXGI_ADAPTER_DESC adapterDesc {};
    dxgiAdapter->GetDesc(&adapterDesc);

    if (!gHooked.test_and_set()) {
      const auto api = ApiLayerApi::Get("d3d11_metrics");
      if (!api) {
        return ret;
      }
      api->AppendLogFrameHook(&LoggingHook);
      dprint("d3d11_metrics: added logging hook");
      api->SetActiveGpu(adapterDesc.AdapterLuid);
      dprint(
        L"d3d11_metrics: detected adapter LUID {:#018x} - {}",
        std::bit_cast<uint64_t>(adapterDesc.AdapterLuid),
        adapterDesc.Description);
      gIsEnabled = true;
    }

    return ret;
  }

  dprint(
    "d3d11_metrics: XrGraphicsBindingD3D11KHR not detected in xrCreateSession");
  return ret;
}

PFN_xrWaitFrame next_xrWaitFrame {nullptr};
XrResult hooked_xrWaitFrame(
  XrSession session,
  const XrFrameWaitInfo* frameWaitInfo,
  XrFrameState* frameState) noexcept {
  const auto ret = next_xrWaitFrame(session, frameWaitInfo, frameState);
  if (!gIsEnabled) {
    return ret;
  }

  if (XR_FAILED(ret)) {
    gWaitedDisplayTime = 0;
    return ret;
  }
  gWaitedDisplayTime = frameState->predictedDisplayTime;
  return ret;
}

PFN_xrDestroySession next_xrDestroySession {nullptr};
XrResult hooked_xrDestroySession(XrSession session) {
  dprint("In d3d11_metrics::xrDestroySession");
  {
    std::unique_lock lock {gFramesMutex};
    gFrames.clear();
    gDevice = nullptr;
    gIsEnabled = false;
  }
  return next_xrDestroySession(session);
}

#define HOOKED_OPENXR_FUNCS(X) \
  X(CreateSession) \
  X(DestroySession) \
  X(WaitFrame) \
  X(BeginFrame) \
  X(EndFrame)

#include "APILayerEntrypoints.inc.cpp"