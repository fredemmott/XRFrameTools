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

struct [[nodiscard]] Popup : NonMoveable {
  inline Popup(const char* name) {
    mActive = ImGui::BeginPopup(name);
  }

  inline ~Popup() {
    if (mActive) {
      ImGui::EndPopup();
    }
  }

  inline operator bool() const noexcept {
    return mActive;
  }

 private:
  bool mActive {};
};

struct [[nodiscard]] PopupModal : NonMoveable {
  inline PopupModal(
    const char* name,
    bool* p_open = NULL,
    ImGuiWindowFlags flags = 0) {
    mActive = ImGui::BeginPopupModal(name, p_open, flags);
  }

  inline ~PopupModal() {
    if (mActive) {
      ImGui::EndPopup();
    }
  }

  inline operator bool() const noexcept {
    return mActive;
  }

 private:
  bool mActive {};
};

struct [[nodiscard]] ImPlot {
  ImPlot(
    const char* title_id,
    const ImVec2& size = ImVec2(-1, 0),
    ImPlotFlags flags = 0) {
    mActive = ::ImPlot::BeginPlot(title_id, size, flags);
  }

  ~ImPlot() {
    if (mActive) {
      ::ImPlot::EndPlot();
    }
  }

  operator bool() const noexcept {
    return mActive;
  }

 private:
  bool mActive {};
};
}// namespace ImGuiScoped