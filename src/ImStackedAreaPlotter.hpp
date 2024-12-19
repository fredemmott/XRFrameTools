// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT
#pragma once

#include <implot.h>

#include <optional>
#include <vector>

class ImStackedAreaPlotter {
 public:
  enum class Kind {
    StackedArea,
    // Allow users to switch between stacked areas and lines without needing
    // two sets of rendering code, e.g. user preference
    Lines,
  };

  explicit ImStackedAreaPlotter(Kind kind = Kind::StackedArea) : mKind(kind) {
  }

  void Plot(const char* name, ImPlotGetter getter, void* data, int count);
  void HideNextItem(ImPlotCond condition = ImPlotCond_Once);

 private:
  Kind mKind {Kind::StackedArea};
  std::vector<ImPlotGetter> mStack;
  std::optional<ImPlotCond> mHideNextItem;
};