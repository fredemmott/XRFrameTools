// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT
#pragma once

#include <imgui.h>
#include <implot.h>

namespace ImGuiScoped {
struct NonMoveable {
  NonMoveable(const NonMoveable&) = delete;
  NonMoveable(const NonMoveable&&) = delete;
  NonMoveable& operator=(const NonMoveable&) = delete;
  NonMoveable& operator=(const NonMoveable&&) = delete;

 protected:
  NonMoveable() = default;
};

struct DisabledIf : NonMoveable {
  inline explicit DisabledIf(bool disabled) {
    ImGui::BeginDisabled(disabled);
  }

  inline ~DisabledIf() {
    ImGui::EndDisabled();
  }
};

struct EnabledIf : DisabledIf {
  inline explicit EnabledIf(bool enabled) : DisabledIf(!enabled) {
  }
};

struct ID : NonMoveable {
  inline ID(const char* name) {
    ImGui::PushID(name);
  }

  inline ~ID() {
    ImGui::PopID();
  };
};

template <auto End>
struct [[nodiscard]] Conditional : NonMoveable {
  Conditional() = delete;
  explicit Conditional(bool isActive) : mIsActive(isActive) {
  }

  ~Conditional() {
    if (mIsActive) {
      std::invoke(End);
    }
  }

  explicit operator bool() const noexcept {
    return mIsActive;
  }

 private:
  bool mIsActive {};
};

struct [[nodiscard]] Popup : Conditional<&ImGui::EndPopup> {
  explicit Popup(const char* name) : Conditional(ImGui::BeginPopup(name)) {
  }
};

struct [[nodiscard]] PopupModal : Conditional<&ImGui::EndPopup> {
  explicit PopupModal(
    const char* name,
    bool* p_open = NULL,
    ImGuiWindowFlags flags = 0)
    : Conditional(ImGui::BeginPopupModal(name, p_open, flags)) {
  }
};

struct [[nodiscard]] ImPlot : Conditional<&::ImPlot::EndPlot> {
  explicit ImPlot(
    const char* title_id,
    const ImVec2& size = ImVec2(-1, 0),
    ImPlotFlags flags = 0)
    : Conditional(::ImPlot::BeginPlot(title_id, size, flags)) {
  }
};

struct [[nodiscard]] TabBar : Conditional<&ImGui::EndTabBar> {
  explicit TabBar(const char* name) : Conditional(ImGui::BeginTabBar(name)) {
  }
};

struct [[nodiscard]] TabItem : Conditional<&ImGui::EndTabItem> {
  explicit TabItem(
    const char* label,
    bool* p_open = NULL,
    ImGuiTabItemFlags flags = 0)
    : Conditional(ImGui::BeginTabItem(label, p_open, flags)) {
  }
};

}// namespace ImGuiScoped