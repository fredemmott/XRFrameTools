// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT

// clang-format off
#include <Windows.h>
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

#include "APILayerAPI.hpp"
#include "FrameMetricsStore.hpp"
#include "Win32Utils.hpp"

namespace {

struct D3D11Frame {
  D3D11Frame() = delete;
  explicit D3D11Frame(ID3D11Device* device, ID3D11DeviceContext* context)
    : mContext(context) {
    D3D11_QUERY_DESC desc {D3D11_QUERY_TIMESTAMP_DISJOINT};
    device->CreateQuery(&desc, mDisjointQuery.put());
    desc = {D3D11_QUERY_TIMESTAMP};
    device->CreateQuery(&desc, mBeginRenderTimestampQuery.put());
    device->CreateQuery(&desc, mEndRenderTimestampQuery.put());
  }

  void StartRender(uint64_t predictedDisplayTime) {
    if (!(mContext && mDisjointQuery && mBeginRenderTimestampQuery)) {
      return;
    }
    mPredictedDisplayTime = predictedDisplayTime;
    mDisplayTime = {};
    mContext->Begin(mDisjointQuery.get());
    // TIMESTAMP queries only have `End()` and `GetData()` called, never
    // `Begin()`
    mContext->End(mBeginRenderTimestampQuery.get());
  }

  void StopRender(uint64_t displayTime) {
    if (!(mContext && mDisjointQuery && mBeginRenderTimestampQuery)) {
      return;
    }
    mDisplayTime = displayTime;
#ifndef NDEBUG
    if (mDisplayTime != mPredictedDisplayTime) {
      dprint("Display time mismatch");
      __debugbreak();
    }
#endif

    mContext->End(mEndRenderTimestampQuery.get());
    mContext->End(mDisjointQuery.get());
  }

  uint64_t GetPredictedDisplayTime() const noexcept {
    return mPredictedDisplayTime;
  }

  uint64_t GetDisplayTime() const noexcept {
    return mDisplayTime;
  }

  enum class GpuDataError {
    Pending,
    Unusable,// e.g. Disjoint
    MissingResources,
  };

  std::expected<uint64_t, GpuDataError> GetRenderMicroseconds() {
    if (!(mContext && mDisjointQuery && mBeginRenderTimestampQuery)) {
      mPredictedDisplayTime = {};
      mDisplayTime = {};
      return std::unexpected {GpuDataError::MissingResources};
    }
    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint {};
    uint64_t begin {};
    uint64_t end {};
    const auto djResult = mContext->GetData(
      mDisjointQuery.get(),
      &disjoint,
      sizeof(disjoint),
      D3D11_ASYNC_GETDATA_DONOTFLUSH);
    if (djResult == S_FALSE) {
      return std::unexpected {GpuDataError::Pending};
    }

    mPredictedDisplayTime = {};
    mDisplayTime = {};
    if (djResult != S_OK) {
      return std::unexpected {GpuDataError::Unusable};
    }
    mContext->GetData(
      mBeginRenderTimestampQuery.get(), &begin, sizeof(begin), 0);
    mContext->GetData(mEndRenderTimestampQuery.get(), &end, sizeof(end), 0);
    if (disjoint.Disjoint || !(disjoint.Frequency && begin && end)) {
      return std::unexpected {GpuDataError::Unusable};
    }

    const auto diff = end - begin;
    const auto gcd = std::gcd(1000000, disjoint.Frequency);
    const auto micros = (diff * (1000000 / gcd)) / (disjoint.Frequency / gcd);

    return micros;
  }

 private:
  ID3D11DeviceContext* mContext {nullptr};
  uint64_t mPredictedDisplayTime {};
  uint64_t mDisplayTime {};

  wil::com_ptr<ID3D11Query> mDisjointQuery;
  wil::com_ptr<ID3D11Query> mBeginRenderTimestampQuery;
  wil::com_ptr<ID3D11Query> mEndRenderTimestampQuery;
};

ID3D11Device* gDevice {nullptr};
wil::com_ptr<ID3D11DeviceContext> gContext;
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
      gFrames.emplace_back(gDevice, gContext.get());
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
  {
    std::unique_lock lock(gFramesMutex);
    auto it = std::ranges::find(
      gFrames, frameEndInfo->displayTime, &D3D11Frame::GetPredictedDisplayTime);
    if (it != gFrames.end()) {
      it->StopRender(frameEndInfo->displayTime);
    }
  }
  return next_xrEndFrame(session, frameEndInfo);
}

static APILayerAPI::LogFrameHookResult LoggingHook(Frame* frame) {
  using Result = APILayerAPI::LogFrameHookResult;
  if (!gIsEnabled) {
    return Result::Ready;
  }

  if (frame->mRenderGpu) {
    return Result::Ready;
  }

  std::unique_lock lock(gFramesMutex);
  auto it = std::ranges::find(
    gFrames, frame->mDisplayTime, &D3D11Frame::GetDisplayTime);
  if (it == gFrames.end()) {
    return Result::Ready;
  }

  const auto timer = it->GetRenderMicroseconds();
  if (timer.has_value()) {
    frame->mRenderGpu = timer.value();
    return Result::Ready;
  }
  using enum D3D11Frame::GpuDataError;
  switch (timer.error()) {
    case Pending:
      return Result::Pending;
    case Unusable:
    case MissingResources:
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
    gDevice->GetImmediateContext(gContext.put());

    if (!gHooked.test_and_set()) {
      const auto coreMetrics = GetModuleHandleW(CoreMetricsDll);

      if (!coreMetrics) {
        dprint(
          "d3d11_metrics: couldn't find core_metrics: {:#010x}",
          static_cast<uint32_t>(HRESULT_FROM_WIN32(GetLastError())));
        return ret;
      }
      auto getter = GetProcAddress(coreMetrics, "XRFrameTools_GetAPILayerAPI");
      if (!getter) {
        dprint("d3d11_metrics: couldn't find inter-layer API entrypoint");
        return ret;
      }
      auto invocable
        = reinterpret_cast<decltype(&XRFrameTools_GetAPILayerAPI)>(getter);
      auto api = invocable(ABIKey, sizeof(ABIKey));
      if (!api) {
        dprint(
          "d3d11_metrics: couldn't get an instance of the inter-layer API");
        return ret;
      }
      api->AppendLogFrameHook(&LoggingHook);
      dprint("d3d11_metrics: added logging hook");
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
  if (XR_FAILED(ret)) {
    gWaitedDisplayTime = 0;
    return ret;
  }
  gWaitedDisplayTime = frameState->predictedDisplayTime;
  return ret;
}

#define HOOKED_OPENXR_FUNCS(X) \
  X(CreateSession) \
  X(WaitFrame) \
  X(BeginFrame) \
  X(EndFrame)

#include "APILayerEntrypoints.inc.cpp"