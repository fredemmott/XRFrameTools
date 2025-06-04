// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT

// clang-format off
#include <Windows.h>
#include <TraceLoggingProvider.h>
// clang-format on

#define XR_USE_GRAPHICS_API_D3D11
#define APILAYER_API __declspec(dllimport)

#include <Knownfolders.h>
#include <d3d11_1.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <openxr/openxr.h>
#include <openxr/openxr_loader_negotiation.h>
#include <openxr/openxr_platform.h>
#include <wil/com.h>

#include <atomic>
#include <expected>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <unordered_map>

#include "CheckHResult.hpp"
#include "SHM.hpp"
#include "SHMReader.hpp"
#include "Win32Utils.hpp"
#include "imgui_impl_win32_headless.hpp"

/* PS>
 * [System.Diagnostics.Tracing.EventSource]::new("XRFrameTools.d3d11_overlay")
 * 602a04c4-e6cf-5e94-e069-d3a167126f04
 */
TRACELOGGING_DEFINE_PROVIDER(
  gTraceProvider,
  "XRFrameTools.d3d11_overlay",
  (0x602a04c4, 0xe6cf, 0x5e94, 0xe0, 0x69, 0xd3, 0xa1, 0x67, 0x12, 0x6f, 0x04));

#define HOOKED_OPENXR_FUNCS(X) \
  X(CreateSession) \
  X(DestroySession) \
  X(CreateSwapchain) \
  X(DestroySwapchain) \
  X(WaitFrame) \
  X(EndFrame)

#define NEXT_OPENXR_FUNCS(X) \
  X(GetInstanceProperties) \
  X(GetSystemProperties) \
  X(EnumerateSwapchainFormats) \
  X(EnumerateSwapchainImages) \
  X(AcquireSwapchainImage) \
  X(WaitSwapchainImage) \
  X(ReleaseSwapchainImage) \
  X(CreateReferenceSpace) \
  X(DestroySpace)

#define DESIRED_OPENXR_EXTENSIONS(X) \
  X(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME)

#define NEXT_OPENXR_EXT_FUNCS(X) \
  X(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME, GetDisplayRefreshRateFB)
namespace {

#define DECLARE_NEXT(IT) PFN_xr##IT next_xr##IT {nullptr};
#define DECLARE_EXT_NEXT(EXT, IT) DECLARE_NEXT(IT)
HOOKED_OPENXR_FUNCS(DECLARE_NEXT)
NEXT_OPENXR_FUNCS(DECLARE_NEXT)
NEXT_OPENXR_EXT_FUNCS(DECLARE_EXT_NEXT)
#undef DECLARE_EXT_NEXT
#undef DECLARE_NEXT

struct SwapchainInfo {
  uint32_t mWidth {};
  uint32_t mHeight {};
};
struct Overlay {
  static constexpr auto Width = 256;
  static constexpr auto Height = 512;
  XrSwapchain mSwapchain {};
  XrSpace mSpace {};
  std::vector<ID3D11Texture2D*> mTextures;
  std::vector<wil::com_ptr<ID3D11RenderTargetView>> mRenderTargetViews;
};

std::mutex gMutex;

ID3D11Device* gDevice {nullptr};
wil::com_ptr<ID3D11DeviceContext1> gContext;
wil::com_ptr<ID3DDeviceContextState> gContextState;

std::string gRuntimeName {};
uint64_t gRuntimeVersion {};
std::string gSystemName {};
uint32_t gMaxLayers {};
std::tuple<uint32_t, uint32_t> gSuggestedSize {};
std::unordered_map<XrSwapchain, SwapchainInfo> gSwapchains;
Overlay gOverlay {};
std::optional<float> gPredictedDisplayPeriod {};
SHMReader gSHMReader;
LARGE_INTEGER gPerformanceCounterFrequency {};

void InitImGui() {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::StyleColorsDark();
  ImGui_ImplWin32_Headless_Init();
  ImGui_ImplDX11_Init(gDevice, gContext.get());
}

void ShutdownImGui() {
  ImGui_ImplDX11_Shutdown();
  ImGui_ImplWin32_Headless_Shutdown();
  ImGui::DestroyContext();
}

std::expected<std::tuple<uint32_t, uint32_t>, XrResult> PaintOverlay(
  ID3D11RenderTargetView* rtv,
  XrSession session,
  const XrFrameEndInfo* frameEndInfo) {
  auto ctx = gContext.get();

  const struct SwapContextState {
    SwapContextState() = delete;
    explicit SwapContextState(ID3D11DeviceContext1* ctx) : mContext(ctx) {
      mContext->SwapDeviceContextState(gContextState.get(), &mOriginalState);
    }
    ~SwapContextState() {
      mContext->SwapDeviceContextState(mOriginalState, nullptr);
    }

   private:
    ID3D11DeviceContext1* mContext {nullptr};
    ID3DDeviceContextState* mOriginalState {nullptr};
  } SwapContextState {ctx};

  constexpr FLOAT BackgroundColor[4] {0.5f, 0.5f, 0.5f, 0.6f};

  ID3D11RenderTargetView* rtvs[] {rtv};
  ctx->OMSetRenderTargets(std::size(rtvs), rtvs, nullptr);
  ctx->ClearRenderTargetView(rtv, BackgroundColor);

  ImGui_ImplWin32_Headless_NewFrame({
    static_cast<float>(Overlay::Width),
    static_cast<float>(Overlay::Height),
  });
  ImGui_ImplDX11_NewFrame();
  ImGui::NewFrame();

  ImGui::SetNextWindowPos({0, 0});
  ImGui::SetNextWindowSize({Overlay::Width, Overlay::Height});
  ImGui::Begin(
    "MainWindow",
    nullptr,
    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
      | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

  ImGui::Text("XRFrameTools - D3D11");
  ImGui::SeparatorText("System");
  ImGui::Text("Headset: %s", gSystemName.c_str());

  ImGui::Text(
    "Runtime:\n  %s\n  v%d.%d.%d",
    gRuntimeName.c_str(),
    XR_VERSION_MAJOR(gRuntimeVersion),
    XR_VERSION_MINOR(gRuntimeVersion),
    XR_VERSION_PATCH(gRuntimeVersion));
  ImGui::Text("Max layers: %d", gMaxLayers);
  ImGui::SeparatorText("Resolution");
  ImGui::Text(
    "Suggested: %d x %d",
    std::get<0>(gSuggestedSize),
    std::get<1>(gSuggestedSize));
  ImGui::Text("Actual:");
  for (uint32_t i = 0; i < frameEndInfo->layerCount; ++i) {
    const auto& layer = frameEndInfo->layers[i];
    switch (layer->type) {
      case XR_TYPE_COMPOSITION_LAYER_PROJECTION: {
        const auto projection
          = reinterpret_cast<const XrCompositionLayerProjection*>(layer);
        ImGui::Text("  %d (projection):", i);
        for (uint32_t j = 0; j < projection->viewCount; ++j) {
          auto& extent = projection->views[j].subImage.imageRect.extent;
          ImGui::Text("    View %u: %dx%d", j, extent.width, extent.height);
        }
        break;
      }
      case XR_TYPE_COMPOSITION_LAYER_QUAD: {
        const auto quad
          = reinterpret_cast<const XrCompositionLayerQuad*>(layer);
        const auto& extent = quad->subImage.imageRect.extent;
        ImGui::Text("  %d (quad): %dx%d", i, extent.width, extent.height);
        break;
      }
      default:
        ImGui::Text("  %d: unrecognized (%ud)", i, layer->type);
        break;
    }
  }
  ImGui::SeparatorText("Performance");
  if (next_xrGetDisplayRefreshRateFB) {
    float refreshRate {};
    if (XR_SUCCEEDED(next_xrGetDisplayRefreshRateFB(session, &refreshRate))) {
      ImGui::Text(
        "%s",
        std::format(
          "Panel:\n  {:.0f}hz ({:.1f}ms)", refreshRate, 1000 / refreshRate)
          .c_str());
    }
  }
  if (gPredictedDisplayPeriod) {
    ImGui::Text(
      "Predicted by runtime:\n  %s",
      std::format(
        "{:.1f}hz ({:.1f}ms)",
        (1000 * 1000 * 1000.0f) / gPredictedDisplayPeriod.value(),
        gPredictedDisplayPeriod.value() / (1000 * 1000.0f))
        .c_str());
  }
  if (gSHMReader.IsValid() && gSHMReader->mFrameCount > 1) {
    const auto& shm = gSHMReader.GetSHM();
    const auto max = shm.mFrameCount - 1;
    const auto min = max > 10 ? max - 10 : 0;

    int64_t previous = 0;
    int64_t sum = 0;
    int64_t worst = 0;
    int count = 0;
    for (uint64_t i = min; i <= max; ++i) {
      const auto time = shm.mFrameMetrics.at(i % SHM::MaxFrameCount)
                          .mCore.mEndFrameStop.QuadPart;
      if (!previous) {
        previous = time;
        continue;
      }
      const auto duration = time - previous;
      previous = time;
      sum += duration;
      if (duration > worst) {
        worst = duration;
      }
      ++count;
    }
    const auto averageMs
      = (1000.0f * sum) / (count * gPerformanceCounterFrequency.QuadPart);
    const auto worstMs
      = (1000.0f * worst) / gPerformanceCounterFrequency.QuadPart;
    ImGui::Text(
      "Average (%d frames):\n  %s",
      count,
      std::format("{:.1f}hz ({:.1f}ms)", 1000 / averageMs, averageMs).c_str());
    ImGui::Text(
      "Worst (%d frames):\n  %s",
      count,
      std::format("{:.1f}hz ({:.1f}ms)", 1000 / worstMs, worstMs).c_str());

    auto& latestFrame = shm.mFrameMetrics.at(max % SHM::MaxFrameCount);
    if ((latestFrame.mValidDataBits
         & std::to_underlying(
           FramePerformanceCounters::ValidDataBits::D3D11))) {
      ImGui::Text(
        "VRAM: %llu MB / %llu MB",
        latestFrame.mVideoMemoryInfo.CurrentUsage / 1024 / 1024,
        latestFrame.mVideoMemoryInfo.Budget / 1024 / 1024);
    }
    if ((latestFrame.mValidDataBits
         & std::to_underlying(
           FramePerformanceCounters::ValidDataBits::NVAPI))) {
      ImGui::Text(
        "GPU throttled: %s",
        latestFrame.mGpuPerformanceInformation.mDecreaseReasons ? "YES" : "no");
    }
  }

  ImGui::End();

  ImGui::Render();
  ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

  return std::tuple {Overlay::Width, Overlay::Height};
}

std::expected<std::tuple<uint32_t, uint32_t>, XrResult> PaintOverlay(
  XrSession session,
  const XrFrameEndInfo* frameEndInfo) {
  auto ctx = gContext.get();

  uint32_t imageIndex {};
  if (const auto res
      = next_xrAcquireSwapchainImage(gOverlay.mSwapchain, nullptr, &imageIndex);
      XR_FAILED(res)) [[unlikely]] {
    dprint("⚠️ xrAcquireSwapchainImage failed: {}", std::to_underlying(res));
    return std::unexpected {res};
  }

  XrSwapchainImageWaitInfo waitInfo {
    .type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO,
    .timeout = XR_INFINITE_DURATION,
  };
  if (const auto res
      = next_xrWaitSwapchainImage(gOverlay.mSwapchain, &waitInfo);
      XR_FAILED(res)) [[unlikely]] {
    dprint("⚠️ xrWaitSwapchainImage failed: {}", std::to_underlying(res));
    return std::unexpected {res};
  }

  auto rtv = gOverlay.mRenderTargetViews.at(imageIndex).get();
  const auto ret = PaintOverlay(rtv, session, frameEndInfo);

  if (const auto res
      = next_xrReleaseSwapchainImage(gOverlay.mSwapchain, nullptr);
      XR_FAILED(res)) [[unlikely]] {
    dprint("⚠️ xrReleaseSwapchainImage failed: {}", std::to_underlying(res));
    return std::unexpected {res};
  }

  return ret;
}

XrResult hooked_xrCreateSwapchain(
  XrSession session,
  const XrSwapchainCreateInfo* createInfo,
  XrSwapchain* swapchain) {
  const auto ret = next_xrCreateSwapchain(session, createInfo, swapchain);
  if (XR_FAILED(ret) || !gDevice) {
    return ret;
  }
  std::unique_lock lock {gMutex};
  gSwapchains.emplace(
    *swapchain, SwapchainInfo {createInfo->width, createInfo->height});
  return ret;
}

XrResult hooked_xrDestroySwapchain(XrSwapchain swapchain) {
  const auto ret = next_xrDestroySwapchain(swapchain);
  if (XR_SUCCEEDED(ret) && gDevice) {
    std::unique_lock lock {gMutex};
    gSwapchains.erase(swapchain);
  }
  return ret;
}

[[nodiscard]]
bool CreateOverlaySwapchain(XrSession session) {
  uint32_t count {};
  if (XR_FAILED(
        next_xrEnumerateSwapchainFormats(session, 0, &count, nullptr))) {
    return false;
  }

  std::vector<int64_t> supportedFormats;
  supportedFormats.resize(count);
  if (XR_FAILED(next_xrEnumerateSwapchainFormats(
        session, count, &count, supportedFormats.data()))) {
    return false;
  }

  struct DesiredFormat {
    DXGI_FORMAT mTextureFormat {};
    DXGI_FORMAT mRenderTargetViewFormat {};
  };
  constexpr DesiredFormat DesiredFormats[] {
    {DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UNORM},
    {DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, DXGI_FORMAT_B8G8R8A8_UNORM},
  };

  std::optional<DesiredFormat> format;
  for (auto&& it: DesiredFormats) {
    if (std::ranges::contains(supportedFormats, it.mTextureFormat)) {
      format = it;
      break;
    }
  }
  if (!format) {
    dprint("⚠️ no supported swapchain format");
    return false;
  }

  XrSwapchainCreateInfo swapchainInfo {
    .type = XR_TYPE_SWAPCHAIN_CREATE_INFO,
    .usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT,
    .format = format->mTextureFormat,
    .sampleCount = 1,
    .width = Overlay::Width,
    .height = Overlay::Height,
    .faceCount = 1,
    .arraySize = 1,
    .mipCount = 1,
  };
  if (const auto res
      = next_xrCreateSwapchain(session, &swapchainInfo, &gOverlay.mSwapchain);
      XR_FAILED(res)) {
    dprint("⚠️ overlay xrCreateSwapchain failed: {}", std::to_underlying(res));
    return false;
  }
  XrReferenceSpaceCreateInfo spaceInfo {
    .type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO,
    .referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW,
    .poseInReferenceSpace = {
      .orientation = {0, 0, 0, 1},
      .position = {0, 0, 0},
    },
  };
  next_xrCreateReferenceSpace(session, &spaceInfo, &gOverlay.mSpace);
  auto sc = gOverlay.mSwapchain;

  if (const auto res = next_xrEnumerateSwapchainImages(sc, 0, &count, nullptr);
      XR_FAILED(res)) {
    dprint(
      "⚠️ overlay xrEnumerateSwapchainImages count failed: {}",
      std::to_underlying(res));
    return false;
  }
  std::vector<XrSwapchainImageD3D11KHR> images {
    count,
    XrSwapchainImageD3D11KHR {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR},
  };
  if (const auto res = next_xrEnumerateSwapchainImages(
        sc,
        count,
        &count,
        reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data()));
      XR_FAILED(res)) {
    dprint(
      "⚠️ overlay xrEnumerateSwapchainImages failed: {}",
      std::to_underlying(res));
    return false;
  }

  const D3D11_RENDER_TARGET_VIEW_DESC rtvDesc {
    .Format = format->mRenderTargetViewFormat,
    .ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D,
  };
  for (auto&& image: images) {
    gOverlay.mTextures.emplace_back(image.texture);
    wil::com_ptr<ID3D11RenderTargetView> rtv;
    gDevice->CreateRenderTargetView(image.texture, &rtvDesc, std::out_ptr(rtv));
    gOverlay.mRenderTargetViews.emplace_back(std::move(rtv));
  }

  return true;
}

XrResult hooked_xrCreateSession(
  XrInstance instance,
  const XrSessionCreateInfo* createInfo,
  XrSession* session) {
  const auto ret = next_xrCreateSession(instance, createInfo, session);
  if (XR_FAILED(ret)) {
    return ret;
  }

  QueryPerformanceFrequency(&gPerformanceCounterFrequency);

  for (auto it = static_cast<const XrBaseInStructure*>(createInfo->next); it;
       it = it->next) {
    if (it->type != XR_TYPE_GRAPHICS_BINDING_D3D11_KHR) {
      continue;
    }
    auto binding = reinterpret_cast<const XrGraphicsBindingD3D11KHR*>(it);
    gDevice = binding->device;
    wil::com_ptr<ID3D11DeviceContext> ctx;
    gDevice->GetImmediateContext(std::out_ptr(ctx));
    gContext = ctx.query<ID3D11DeviceContext1>();
    wil::com_ptr<ID3D11Device1> device1;
    gDevice->QueryInterface(device1.put());

    D3D_FEATURE_LEVEL levels[] {
      D3D_FEATURE_LEVEL_11_1,
      D3D_FEATURE_LEVEL_11_0,
    };
    device1->CreateDeviceContextState(
      0,
      levels,
      std::size(levels),
      D3D11_SDK_VERSION,
      __uuidof(ID3D11Device),
      nullptr,
      gContextState.put());
    break;
  }

  if (!gDevice) {
    dprint("[{}] ❌ app is not using D3D11", API_LAYER_TARGET_NAME);
    return ret;
  }
  dprint("[{}] ✅ app is using D3D11", API_LAYER_TARGET_NAME);

  if (!CreateOverlaySwapchain(*session)) {
    return ret;
  }

  XrInstanceProperties instanceProperties {XR_TYPE_INSTANCE_PROPERTIES};
  if (XR_FAILED(next_xrGetInstanceProperties(instance, &instanceProperties))) {
    return ret;
  }
  XrSystemProperties systemProperties {XR_TYPE_SYSTEM_PROPERTIES};
  if (XR_FAILED(next_xrGetSystemProperties(
        instance, createInfo->systemId, &systemProperties))) {
    return ret;
  }

  gRuntimeName = std::string {
    instanceProperties.runtimeName,
    strnlen_s(instanceProperties.runtimeName, XR_MAX_RUNTIME_NAME_SIZE)};
  gRuntimeVersion = instanceProperties.runtimeVersion;

  gSystemName = std::string {
    systemProperties.systemName,
    strnlen_s(systemProperties.systemName, XR_MAX_SYSTEM_NAME_SIZE)};
  gMaxLayers = systemProperties.graphicsProperties.maxLayerCount;
  gSuggestedSize = {
    systemProperties.graphicsProperties.maxSwapchainImageWidth,
    systemProperties.graphicsProperties.maxSwapchainImageHeight,
  };

  dprint(
    "[{}] '{}', running on '{}' v{}.{}.{}",
    API_LAYER_TARGET_NAME,
    gSystemName,
    gRuntimeName,
    XR_VERSION_MAJOR(gRuntimeVersion),
    XR_VERSION_MINOR(gRuntimeVersion),
    XR_VERSION_PATCH(gRuntimeVersion));
  dprint(
    "[{}] max of {} layers, with a suggested resolution of {}x{}",
    API_LAYER_TARGET_NAME,
    gMaxLayers,
    std::get<0>(gSuggestedSize),
    std::get<1>(gSuggestedSize));

  InitImGui();

  return ret;
}

XrResult hooked_xrDestroySession(XrSession session) {
  if (gOverlay.mSwapchain) {
    next_xrDestroySwapchain(gOverlay.mSwapchain);
    next_xrDestroySpace(gOverlay.mSpace);
    gOverlay = {};
  }
  const auto ret = next_xrDestroySession(session);

  if (gDevice) {
    ShutdownImGui();
    gContextState = nullptr;
    gContext = nullptr;
    gDevice = nullptr;
  }
  return ret;
}

XrResult hooked_xrWaitFrame(
  XrSession session,
  const XrFrameWaitInfo* waitInfo,
  XrFrameState* frameState) {
  const auto ret = next_xrWaitFrame(session, waitInfo, frameState);
  if (XR_FAILED(ret) || !gDevice) {
    return ret;
  }
  gPredictedDisplayPeriod = frameState->predictedDisplayPeriod;
  return ret;
}

XrResult hooked_xrEndFrame(
  XrSession session,
  const XrFrameEndInfo* frameEndInfo) noexcept {
  const auto passthrough
    = std::bind_front(next_xrEndFrame, session, frameEndInfo);

  if (frameEndInfo->layerCount >= gMaxLayers || !gDevice) {
    return passthrough();
  }

  const auto overlayDimensions = PaintOverlay(session, frameEndInfo);
  if (!overlayDimensions.has_value()) {
    return overlayDimensions.error();
  }

  std::vector<const XrCompositionLayerBaseHeader*> nextLayers;
  nextLayers.reserve(frameEndInfo->layerCount + 1);
  nextLayers.append_range(
    std::span {frameEndInfo->layers, frameEndInfo->layerCount});

  XrCompositionLayerQuad overlayLayer {
    .type = XR_TYPE_COMPOSITION_LAYER_QUAD,
    .layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT,
    .space = gOverlay.mSpace,
    .subImage = {
      .swapchain = gOverlay.mSwapchain,
      .imageRect = {
        {0, 0},
        {Overlay::Width, Overlay::Height},
      },
    },
    .pose = {
      .orientation = { 0, 0, 0, 1 },
      .position = { 0, 0, -0.2 },
    },
    .size = {0.1f, 0.2f}, // TODO
  };
  nextLayers.emplace_back(
    reinterpret_cast<XrCompositionLayerBaseHeader*>(&overlayLayer));

  XrFrameEndInfo nextFrameEndInfo {*frameEndInfo};
  nextFrameEndInfo.layerCount = nextLayers.size();
  nextFrameEndInfo.layers = nextLayers.data();

  const auto ret = next_xrEndFrame(session, &nextFrameEndInfo);
  if (XR_FAILED(ret)) {
    return ret;
  }

  return ret;
}

}// namespace

#include "APILayerEntrypoints.inc.cpp"