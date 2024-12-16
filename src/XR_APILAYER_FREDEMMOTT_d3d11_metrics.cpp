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
#include <format>
#include <numeric>
#include <span>

#include "APILayerAPI.hpp"
#include "FrameMetricsStore.hpp"
#include "Win32Utils.hpp"

namespace {
struct D3D11Frame {
  D3D11Frame() = default;
  explicit D3D11Frame(ID3D11Device* device) {
    device->GetImmediateContext(mContext.put());

    D3D11_QUERY_DESC desc {D3D11_QUERY_TIMESTAMP_DISJOINT};
    device->CreateQuery(&desc, mDisjointQuery.put());
    desc = {D3D11_QUERY_TIMESTAMP};
    device->CreateQuery(&desc, mBeginRenderTimestampQuery.put());
    device->CreateQuery(&desc, mEndRenderTimestampQuery.put());
  }

  void StartRender() {
    if (!(mContext && mDisjointQuery && mBeginRenderTimestampQuery)) {
      return;
    }
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
    mContext->End(mEndRenderTimestampQuery.get());
    mContext->End(mDisjointQuery.get());
  }

  uint64_t GetDisplayTime() const noexcept {
    return mDisplayTime;
  }

  uint64_t GetRenderMicroseconds() {
    if (!(mContext && mDisjointQuery && mBeginRenderTimestampQuery)) {
      return 0;
    }

    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint {};
    uint64_t begin {};
    uint64_t end {};
    mContext->GetData(mDisjointQuery.get(), &disjoint, sizeof(disjoint), 0);
    mContext->GetData(
      mBeginRenderTimestampQuery.get(), &begin, sizeof(begin), 0);
    mContext->GetData(mEndRenderTimestampQuery.get(), &end, sizeof(end), 0);
    if (disjoint.Disjoint || !(disjoint.Frequency && begin && end)) {
      return 0;
    }

    const auto diff = end - begin;
    const auto gcd = std::gcd(1000000, disjoint.Frequency);
    const auto micros = (diff * (1000000 / gcd)) / (disjoint.Frequency / gcd);
    return micros;
  }

 private:
  uint64_t mDisplayTime {};

  wil::com_ptr<ID3D11DeviceContext> mContext;
  wil::com_ptr<ID3D11Query> mDisjointQuery;
  wil::com_ptr<ID3D11Query> mBeginRenderTimestampQuery;
  wil::com_ptr<ID3D11Query> mEndRenderTimestampQuery;
};

std::array<D3D11Frame, 3> gFrames;
std::atomic<std::size_t> gBeginFrameCounter {0};
std::atomic<std::size_t> gEndFrameCounter {0};
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

  auto& frame = gFrames[gBeginFrameCounter++ % gFrames.size()];
  frame.StartRender();

  return ret;
}

PFN_xrEndFrame next_xrEndFrame {nullptr};
XrResult hooked_xrEndFrame(
  XrSession session,
  const XrFrameEndInfo* frameEndInfo) noexcept {
  auto& frame = gFrames[gEndFrameCounter++ % gFrames.size()];
  frame.StopRender(frameEndInfo->displayTime);
  return next_xrEndFrame(session, frameEndInfo);
}

static void LoggingHook(Frame& frame) {
  if (!gIsEnabled) {
    return;
  }

  auto it = std::ranges::find(
    gFrames, frame.mDisplayTime, &D3D11Frame::GetDisplayTime);
  if (it == gFrames.end()) {
    __debugbreak();
    return;
  }
  const auto timer = it->GetRenderMicroseconds();
  if (timer) {
    frame.mRenderGpu = timer;
  }
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

    auto graphicsBinding
      = reinterpret_cast<const XrGraphicsBindingD3D11KHR*>(it);

    for (auto&& frame: gFrames) {
      frame = D3D11Frame {graphicsBinding->device};
    }

    dprint("d3d11_metrics: session created");

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

#define HOOKED_OPENXR_FUNCS(X) \
  X(CreateSession) \
  X(BeginFrame) \
  X(EndFrame)

#include "APILayerEntrypoints.inc.cpp"