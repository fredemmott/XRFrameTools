// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT

// clang-format off
#include <Windows.h>
#include <TraceLoggingProvider.h>
#include <TraceLoggingActivity.h>
#include <evntprov.h>
// clang-format on

#include <openxr/openxr.h>
#include <openxr/openxr_loader_negotiation.h>

#include <format>
#include <ranges>

#include "FramePerformanceCounters.hpp"
#include "SHM.hpp"

#define HOOKED_OPENXR_FUNCS(X) \
  X(WaitFrame) \
  X(BeginFrame) \
  X(EndFrame)

/* PS>
 * [System.Diagnostics.Tracing.EventSource]::new("XRFrameTools")
 * a6efd5fe-e082-5e08-69da-0a9fcdafda5f
 */
TRACELOGGING_DEFINE_PROVIDER(
  gTraceProvider,
  "XRFrameTools",
  (0xa6efd5fe, 0xe082, 0x5e08, 0x69, 0xda, 0x0a, 0x9f, 0xcd, 0xaf, 0xda, 0x5f));

static SHMWriter gSHM;

struct FrameActivities {
  TraceLoggingActivity<gTraceProvider> mFrameActivity;
  TraceLoggingActivity<gTraceProvider> mWaitFrameActivity;
  TraceLoggingActivity<gTraceProvider> mBeginFrameActivity;
  TraceLoggingActivity<gTraceProvider> mEndFrameActivity;
};

struct Frame final : FramePerformanceCounters, FrameActivities {
  Frame() = default;
  ~Frame() = default;

  Frame(const Frame&) = delete;
  Frame(Frame&&) = delete;
  Frame& operator=(const Frame&) = delete;
  Frame& operator=(Frame&&) = delete;

  uint64_t mDisplayTime {};

  // The TraceLoggingActivities are non-copyable and non-moveable, so we can't
  // just do `= {}`.
  void Reset() {
    this->~Frame();
    new (this) Frame();
  }
};

class FrameMetricsStore {
 public:
  static Frame& GetForEndFrame(uint64_t displayTime) noexcept {
    const auto it
      = std::ranges::find(mFrames, displayTime, &Frame::mDisplayTime);
    if (it == mFrames.end()) [[unlikely]] {
      OutputDebugStringA(
        std::format(
          "XRFrameTools: Couldn't find an in-progress frame with display time "
          "{}",
          displayTime)
          .c_str());
      __debugbreak();
    }

    return *it;
  }

  static Frame& GetForWaitFrame() noexcept {
    const auto it = std::ranges::find_if(mFrames, [](const Frame& frame) {
      return frame.mWaitFrameStart.QuadPart == 0;
    });

    if (it == mFrames.end()) [[unlikely]] {
      OutputDebugStringA("XRFrameTools: No frames ready for wait");
      __debugbreak();
    }
    return *it;
  }

  static Frame& GetForBeginFrame() noexcept {
    const auto it = std::ranges::find_if(mFrames, [](const Frame& frame) {
      return (frame.mWaitFrameStop.QuadPart != 0)
        && (frame.mBeginFrameStart.QuadPart == 0);
    });

    if (it == mFrames.end()) [[unlikely]] {
      OutputDebugStringA("XRFrameTools: No frames ready for begin");
      __debugbreak();
    }
    return *it;
  }

 private:
  static std::array<Frame, 3> mFrames;
};
decltype(FrameMetricsStore::mFrames) FrameMetricsStore::mFrames {};

bool gHaveError = false;

PFN_xrWaitFrame next_xrWaitFrame {nullptr};
XrResult hooked_xrWaitFrame(
  XrSession session,
  const XrFrameWaitInfo* frameWaitInfo,
  XrFrameState* frameState) noexcept {
  if (gHaveError) {
    return next_xrWaitFrame(session, frameWaitInfo, frameState);
  }
  auto& frame = FrameMetricsStore::GetForWaitFrame();

  TraceLoggingWriteStart(frame.mFrameActivity, "Frame");
  TraceLoggingWriteStart(frame.mWaitFrameActivity, "xrWaitFrame");
  QueryPerformanceCounter(&frame.mWaitFrameStart);
  const auto ret = next_xrWaitFrame(session, frameWaitInfo, frameState);
  QueryPerformanceCounter(&frame.mWaitFrameStop);
  TraceLoggingWriteStop(
    frame.mWaitFrameActivity,
    "xrWaitFrame",
    TraceLoggingValue(frameState->predictedDisplayTime, "predictedDisplayTime"),
    TraceLoggingValue(std::to_underlying(ret), "XrResult"));

  if (XR_SUCCEEDED(ret)) [[likely]] {
    frame.mDisplayTime = frameState->predictedDisplayTime;
    return ret;
  }

  gHaveError = true;
  return ret;
}

PFN_xrBeginFrame next_xrBeginFrame {nullptr};
XrResult hooked_xrBeginFrame(
  XrSession session,
  const XrFrameBeginInfo* frameBeginInfo) noexcept {
  if (gHaveError) {
    return next_xrBeginFrame(session, frameBeginInfo);
  }

  auto& frame = FrameMetricsStore::GetForBeginFrame();

  TraceLoggingWriteStart(
    frame.mBeginFrameActivity,
    "xrBeginFrame",
    TraceLoggingValue(frame.mDisplayTime, "displayTime"));
  QueryPerformanceCounter(&frame.mBeginFrameStart);
  const auto ret = next_xrBeginFrame(session, frameBeginInfo);
  QueryPerformanceCounter(&frame.mBeginFrameStop);
  TraceLoggingWriteStop(
    frame.mBeginFrameActivity,
    "xrBeginFrame",
    TraceLoggingValue(std::to_underlying(ret), "XrResult"));

  if (XR_FAILED(ret)) [[unlikely]] {
    gHaveError = true;
  }

  return ret;
}

PFN_xrEndFrame next_xrEndFrame {nullptr};
XrResult hooked_xrEndFrame(
  XrSession session,
  const XrFrameEndInfo* frameEndInfo) noexcept {
  if (gHaveError) {
    return next_xrEndFrame(session, frameEndInfo);
  }

  auto& frame = FrameMetricsStore::GetForEndFrame(frameEndInfo->displayTime);
  TraceLoggingWriteStart(
    frame.mEndFrameActivity,
    "EndFrame",
    TraceLoggingValue(frameEndInfo->displayTime, "displayTime"));
  QueryPerformanceCounter(&frame.mEndFrameStart);
  const auto ret = next_xrEndFrame(session, frameEndInfo);
  QueryPerformanceCounter(&frame.mEndFrameStop);
  TraceLoggingWriteStop(
    frame.mEndFrameActivity,
    "EndFrame",
    TraceLoggingValue(std::to_underlying(ret), "XrResult"));

  if (XR_FAILED(ret)) [[unlikely]] {
    gHaveError = true;
  }

  gSHM.LogFrame(frame);
  frame.Reset();
  return ret;
}

static bool gEnabled = true;

template <class F, auto Next, auto Layer>
struct XRFuncDelegator;

template <class TRet, class... TArgs, auto LayerFn, auto NextFn>
struct XRFuncDelegator<TRet (*)(TArgs...), LayerFn, NextFn> {
  static XRAPI_ATTR TRet XRAPI_CALL Invoke(TArgs... args) noexcept {
    if (!*NextFn) {
      return XR_ERROR_FUNCTION_UNSUPPORTED;
    }

    if (gHaveError || !gEnabled) {
      return std::invoke(*NextFn, args...);
    }
    return std::invoke(LayerFn, args...);
  }
};

PFN_xrGetInstanceProcAddr next_xrGetInstanceProcAddr {nullptr};
XRAPI_ATTR XrResult XRAPI_CALL xrGetInstanceProcAddr(
  XrInstance instance,
  const char* name,
  PFN_xrVoidFunction* function) {
  if (!next_xrGetInstanceProcAddr) {
    return XR_ERROR_FUNCTION_UNSUPPORTED;
  }

  const std::string_view nameView {name};

#define HOOK(FN) \
  if (nameView == "xr" #FN) { \
    *function = reinterpret_cast<PFN_xrVoidFunction>( \
      &XRFuncDelegator<PFN_xr##FN, &hooked_xr##FN, &next_xr##FN>::Invoke); \
    return XR_SUCCESS; \
  }
  HOOKED_OPENXR_FUNCS(HOOK)
#undef HOOK

  return next_xrGetInstanceProcAddr(instance, name, function);
}

XRAPI_ATTR XrResult XRAPI_CALL xrCreateApiLayerInstance(
  const XrInstanceCreateInfo* info,
  const struct XrApiLayerCreateInfo* layerInfo,
  XrInstance* instance) {
  next_xrGetInstanceProcAddr = layerInfo->nextInfo->nextGetInstanceProcAddr;

  auto nextLayerInfo = *layerInfo;
  nextLayerInfo.nextInfo = nextLayerInfo.nextInfo->next;
  const auto ret = layerInfo->nextInfo->nextCreateApiLayerInstance(
    info, &nextLayerInfo, instance);
  if (XR_FAILED(ret)) {
    return ret;
  }

#define INIT_NEXT(FUNC) \
  next_xrGetInstanceProcAddr( \
    *instance, \
    "xr" #FUNC, \
    reinterpret_cast<PFN_xrVoidFunction*>(&next_xr##FUNC));
  HOOKED_OPENXR_FUNCS(INIT_NEXT)
#undef INIT_NEXT

  return ret;
}

extern "C" __declspec(dllexport) XRAPI_ATTR XrResult XRAPI_CALL
xrNegotiateLoaderApiLayerInterface(
  const XrNegotiateLoaderInfo* loaderInfo,
  const char* layerName,
  XrNegotiateApiLayerRequest* apiLayerRequest) {
  // "The API layer **must**..."
  if (loaderInfo->structType != XR_LOADER_INTERFACE_STRUCT_LOADER_INFO) {
    OutputDebugStringA("XRFrameTools: Bad loaderInfo structType");
    return XR_ERROR_INITIALIZATION_FAILED;
  }
  if (loaderInfo->structVersion != XR_LOADER_INFO_STRUCT_VERSION) {
    OutputDebugStringA("XRFrameTools: Bad loaderInfo structVersion");
    return XR_ERROR_INITIALIZATION_FAILED;
  }
  if (loaderInfo->structSize != sizeof(XrNegotiateLoaderInfo)) {
    OutputDebugStringA("XRFrameTools: Bad loaderInfo structSize");
    return XR_ERROR_INITIALIZATION_FAILED;
  }
  if (
    apiLayerRequest->structType
    != XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST) {
    OutputDebugStringA("XRFrameTools: Bad apiLayerRequest structType");
    return XR_ERROR_INITIALIZATION_FAILED;
  }
  if (apiLayerRequest->structVersion != XR_API_LAYER_INFO_STRUCT_VERSION) {
    OutputDebugStringA("XRFrameTools: Bad apiLayerRequest structVersion");
    return XR_ERROR_INITIALIZATION_FAILED;
  }
  if (apiLayerRequest->structSize != sizeof(XrNegotiateApiLayerRequest)) {
    OutputDebugStringA("XRFrameTools: Bad apiLayerRequest structSize");
    return XR_ERROR_INITIALIZATION_FAILED;
  }

  // Return our info
  const bool supports1_0 = XR_API_VERSION_1_0 >= loaderInfo->minApiVersion
    && XR_API_VERSION_1_0 <= loaderInfo->maxApiVersion;
  const bool supports1_1 = XR_API_VERSION_1_1 >= loaderInfo->minApiVersion
    && XR_API_VERSION_1_1 <= loaderInfo->maxApiVersion;

  if (!(supports1_0 || supports1_1)) {
    OutputDebugStringA("XRFrameTools: No compatible OpenXR version");
    return XR_ERROR_INITIALIZATION_FAILED;
  }
  if (supports1_1) {
    OutputDebugStringA("XRFrameTools: Using OpenXR version 1.1");
    apiLayerRequest->layerApiVersion = XR_API_VERSION_1_1;
  } else {
    OutputDebugStringA("XRFrameTools: Using OpenXR version 1.0");
    apiLayerRequest->layerApiVersion = XR_API_VERSION_1_0;
  }

  apiLayerRequest->getInstanceProcAddr = &xrGetInstanceProcAddr;
  apiLayerRequest->createApiLayerInstance = &xrCreateApiLayerInstance;

  return XR_SUCCESS;
}