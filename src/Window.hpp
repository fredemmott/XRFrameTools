// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT

#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <imgui.h>
#include <wil/com.h>
#include <wil/resource.h>

#include <optional>
#include <string>

class Window {
 public:
  Window() = delete;

  explicit Window(const HINSTANCE instance, const std::wstring& title);
  virtual ~Window();

  [[nodiscard]]
  HWND GetHWND() const noexcept;
  [[nodiscard]] int Run() noexcept;

 protected:
  virtual void RenderContent() = 0;

  virtual std::optional<float> GetTargetFPS() const noexcept {
    return std::nullopt;
  }

  wil::com_ptr<ID3D11Device> mD3DDevice;
  wil::com_ptr<ID3D11DeviceContext> mD3DContext;

 private:
  static Window* gInstance;
  wil::unique_hwnd mHwnd;
  wil::com_ptr<IDXGISwapChain1> mSwapChain;
  wil::com_ptr<ID3D11RenderTargetView> mRenderTargetView;
  std::optional<int> mExitCode;
  ImVec2 mWindowSize;
  std::optional<std::tuple<UINT, UINT>> mPendingResize;

  static LRESULT
  WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) noexcept;

  void RenderWindow() noexcept;

  void InitializeHWND(HINSTANCE instance, const std::wstring& title);
  void InitializeDirect3D();
  void InitializeFonts();
};