// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT

#include "Window.hpp"

#include <TraceLoggingActivity.h>
#include <TraceLoggingProvider.h>
#include <dxgi1_3.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <shlobj_core.h>
#include <wil/com.h>
#include <wil/registry.h>

#include <format>
#include <queue>
#include <stdexcept>
#include <thread>

#include "CheckHResult.hpp"
#include "D3D11GpuTimer.hpp"
#include "Win32Utils.hpp"

TRACELOGGING_DECLARE_PROVIDER(gTraceProvider);
static std::queue<D3D11GpuTimer> gPendingTimers;
static std::queue<D3D11GpuTimer> gAvailableTimers;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
  HWND hWnd,
  UINT msg,
  WPARAM wParam,
  LPARAM lParam);

void Window::InitializeHWND(
  const HINSTANCE instance,
  const std::wstring& title) {
  const WNDCLASSW wc {
    .lpfnWndProc = &WindowProc,
    .hInstance = instance,
    .lpszClassName = L"XRFrameTools",
  };
  const auto classAtom = RegisterClassW(&wc);

  const auto screenHeight = GetSystemMetrics(SM_CYSCREEN);
  const auto height = screenHeight * 2 / 3;
  const auto width = height * 2;

  // With the UTF-8 manifest, things go *really* weird if we use
  // CreateWindowExA, we take the title as UTF-16 and use CreateWindow*W
  mHwnd.reset(CreateWindowExW(
    WS_EX_APPWINDOW | WS_EX_CLIENTEDGE,
    reinterpret_cast<LPCWSTR>(MAKEINTATOM(classAtom)),
    title.c_str(),
    WS_OVERLAPPEDWINDOW & (~WS_MAXIMIZEBOX),
    CW_USEDEFAULT,
    CW_USEDEFAULT,
    width,
    height,
    nullptr,
    nullptr,
    instance,
    nullptr));

  if (!mHwnd) {
    throw std::runtime_error(
      std::format("Failed to create window: {}", GetLastError()));
  }
}
void Window::InitializeDirect3D() {
  UINT d3dFlags = D3D11_CREATE_DEVICE_SINGLETHREADED;
  UINT dxgiFlags = 0;
#ifndef NDEBUG
  d3dFlags |= D3D11_CREATE_DEVICE_DEBUG;
  dxgiFlags |= DXGI_CREATE_FACTORY_DEBUG;
#endif

  CheckHResult(D3D11CreateDevice(
    nullptr,
    D3D_DRIVER_TYPE_HARDWARE,
    nullptr,
    d3dFlags,
    nullptr,
    0,
    D3D11_SDK_VERSION,
    mD3DDevice.put(),
    nullptr,
    mD3DContext.put()));

  wil::com_ptr<IDXGIFactory3> dxgiFactory;
  CheckHResult(CreateDXGIFactory2(dxgiFlags, IID_PPV_ARGS(dxgiFactory.put())));

  DXGI_SWAP_CHAIN_DESC1 swapChainDesc {
    .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
    .SampleDesc = {1, 0},
    .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
    .BufferCount = 3,
    .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
    .AlphaMode = DXGI_ALPHA_MODE_IGNORE,
  };
  mWindowSize = {
    static_cast<FLOAT>(swapChainDesc.Width),
    static_cast<FLOAT>(swapChainDesc.Height),
  };
  CheckHResult(dxgiFactory->CreateSwapChainForHwnd(
    mD3DDevice.get(),
    mHwnd.get(),
    &swapChainDesc,
    nullptr,
    nullptr,
    mSwapChain.put()));
  mSwapChain->GetDesc1(&swapChainDesc);
  mWindowSize = {
    static_cast<FLOAT>(swapChainDesc.Width),
    static_cast<FLOAT>(swapChainDesc.Height),
  };

  wil::com_ptr<ID3D11Texture2D> backBuffer;
  CheckHResult(mSwapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.put())));
  CheckHResult(mD3DDevice->CreateRenderTargetView(
    backBuffer.get(), nullptr, mRenderTargetView.put()));
}

Window::Window(const HINSTANCE instance, const std::wstring& title) {
  gInstance = this;

  this->InitializeHWND(instance, title);
  this->InitializeDirect3D();

  auto& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

  ImGui_ImplWin32_Init(mHwnd.get());
  ImGui_ImplDX11_Init(mD3DDevice.get(), mD3DContext.get());

  this->InitializeFonts();
}

Window::~Window() {
  ImGui_ImplDX11_Shutdown();
  ImGui_ImplWin32_Shutdown();
  ImGui::DestroyContext();
  gInstance = nullptr;
}

void Window::InitializeFonts() {
  const auto path = GetKnownFolderPath(FOLDERID_Fonts);
  if (path.empty()) {
    return;
  }

  const auto fonts = ImGui::GetIO().Fonts;

  fonts->Clear();

  const auto dpi = GetDpiForWindow(mHwnd.get());
  const auto scale = static_cast<FLOAT>(dpi) / USER_DEFAULT_SCREEN_DPI;
  const auto size = std::floorf(scale * 16);

  fonts->AddFontFromFileTTF((path / "segoeui.ttf").string().c_str(), size);

  ImWchar ranges[] = {
    // clang-format off
    0x1,
    0xFFFF,
    0x0000,
    // clang-format on
  };
  ImFontConfig config;
  config.OversampleH = config.OversampleV = 1;
  config.MergeMode = true;
  config.GlyphOffset = {0, size / 5};
  config.GlyphMinAdvanceX = size * 2;
  const auto fluidIcons = (path / "SegoeIcons.ttf");// Win11+
  const auto mdlIcons = (path / "segmdl2.ttf");// Win10+
  const auto icons
    = std::filesystem::exists(fluidIcons) ? fluidIcons : mdlIcons;
  fonts->AddFontFromFileTTF(icons.string().c_str(), size, &config, ranges);

  fonts->Build();
  ImGui_ImplDX11_InvalidateDeviceObjects();

  ImGui::GetStyle().ScaleAllSizes(scale);
}

HWND Window::GetHWND() const noexcept {
  return mHwnd.get();
}

int Window::Run() noexcept {
  while (!mExitCode) {
    const auto thisFrameStart = std::chrono::steady_clock::now();
    if (mPendingResize) {
      mRenderTargetView.reset();
      const auto width = std::get<0>(*mPendingResize);
      const auto height = std::get<1>(*mPendingResize);
      mSwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
      wil::com_ptr<ID3D11Texture2D> backBuffer;
      CheckHResult(mSwapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.put())));
      CheckHResult(mD3DDevice->CreateRenderTargetView(
        backBuffer.get(), nullptr, mRenderTargetView.put()));

      mWindowSize = {
        static_cast<FLOAT>(width),
        static_cast<FLOAT>(height),
      };
      mPendingResize = std::nullopt;
    }

    {
      MSG msg {};
      TraceLoggingThreadActivity<gTraceProvider> activity;
      TraceLoggingWriteStart(activity, "Window::Run/WM");
      while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
        if (msg.message == WM_QUIT) {
          return mExitCode.value_or(0);
        }
      }
      TraceLoggingWriteStop(activity, "Window::Run/WM");
    }
    while (!gPendingTimers.empty()) {
      auto& it = gPendingTimers.front();
      const auto result = it.GetMicroseconds();
      if (result == std::unexpected {GpuDataError::Pending}) {
        break;
      }

      if (result.has_value()) {
        TraceLoggingWrite(
          gTraceProvider,
          "XRFT app frame GPU time",
          TraceLoggingValue(*result, "micros"));
      }

      gAvailableTimers.push(std::move(it));
      gPendingTimers.pop();
    }
    if (gAvailableTimers.empty()) {
      gAvailableTimers.emplace(mD3DDevice.get());
    }
    auto timer = std::move(gAvailableTimers.front());
    gAvailableTimers.pop();
    timer.Start();

    const auto rawRTV = mRenderTargetView.get();
    FLOAT clearColor[4] {0.0f, 0.0f, 0.0f, 1.0f};
    mD3DContext->ClearRenderTargetView(rawRTV, clearColor);
    mD3DContext->OMSetRenderTargets(1, &rawRTV, nullptr);

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    {
      TraceLoggingThreadActivity<gTraceProvider> activity;
      TraceLoggingWriteStart(activity, "Window::Run/RenderWindow");
      this->RenderWindow();
      TraceLoggingWriteStop(activity, "Window::Run/RenderWindow");
    }

    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    timer.Stop();
    gPendingTimers.push(std::move(timer));
    {
      TraceLoggingThreadActivity<gTraceProvider> activity;
      TraceLoggingWriteStart(activity, "Window::Run/Present");
      mSwapChain->Present(0, 0);
      TraceLoggingWriteStop(activity, "Window::Run/Present");
    }

    TraceLoggingThreadActivity<gTraceProvider> waitActivity;
    TraceLoggingWriteStart(waitActivity, "Window::Run/wait");
    const auto endActivity = wil::scope_exit(
      [&]() { TraceLoggingWriteStop(waitActivity, "Window::Run/wait"); });

    const auto fps = this->GetTargetFPS();
    if (!fps) {
      WaitMessage();
      continue;
    }
    const auto thisFrameTime
      = std::chrono::steady_clock::now() - thisFrameStart;
    const auto desiredFrameTime = (std::chrono::milliseconds(1000) / (*fps));
    if (desiredFrameTime < thisFrameTime) {
      continue;
    }
    const auto interval = static_cast<DWORD>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
        (desiredFrameTime - thisFrameTime))
        .count());
    TraceLoggingWrite(
      gTraceProvider,
      "Widow::Run/MsgWaitForMultipleObjects",
      TraceLoggingInt32(interval, "milliseconds"));
    MsgWaitForMultipleObjects(0, nullptr, FALSE, interval, QS_ALLINPUT);
  }

  return *mExitCode;
}

LRESULT
Window::WindowProc(
  HWND hwnd,
  UINT uMsg,
  WPARAM wParam,
  LPARAM lParam) noexcept {
  if (ImGui_ImplWin32_WndProcHandler(hwnd, uMsg, wParam, lParam)) {
    return true;
  }
  if (uMsg == WM_SIZE) {
    const UINT width = LOWORD(lParam);
    const UINT height = HIWORD(lParam);
    gInstance->mPendingResize = std::tuple {width, height};
    return 0;
  }
  if (uMsg == WM_CLOSE) {
    gInstance->mExitCode = 0;
  }
  return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

void Window::RenderWindow() noexcept {
  ImGui::SetNextWindowPos({0, 0});
  ImGui::SetNextWindowSize(mWindowSize);
  ImGui::Begin(
    "MainWindow",
    nullptr,
    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
      | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

  this->RenderContent();

  ImGui::End();
}

Window* Window::gInstance {nullptr};