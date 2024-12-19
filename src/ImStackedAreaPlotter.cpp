// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT

#include "ImStackedAreaPlotter.hpp"

#include <implot_internal.h>

#include <span>
namespace {
[[nodiscard]]
bool GetLastItemHidden() {
  if (!GImPlot->PreviousItem) {
    return false;
  }
  return !GImPlot->PreviousItem->Show;
}

struct NestedData {
  ImPlotGetter mXGetter {nullptr};
  std::span<ImPlotGetter> mYStack;
  void* mUserData {nullptr};
};

ImPlotPoint PlotStacked(int idx, void* data) {
  const auto& args = *static_cast<NestedData*>(data);

  ImPlotPoint ret = {args.mXGetter(idx, args.mUserData).x, 0};
  for (const auto& getter: args.mYStack) {
    const auto it = getter(idx, args.mUserData);
    ret.y += it.y;
  }
  return ret;
}
}// namespace

void ImStackedAreaPlotter::Plot(
  const char* name,
  ImPlotGetter getter,
  void* data,
  int count) {
  mStack.push_back(getter);

  NestedData topData {
    .mXGetter = getter,
    .mYStack = {mStack},
    .mUserData = data,
  };
  NestedData bottomData {
    .mXGetter = getter,
    .mYStack = {mStack.begin(), --mStack.end()},
    .mUserData = data,
  };

  if (mKind == Kind::StackedArea) {
    if (mHideNextItem) {
      ImPlot::HideNextItem(*mHideNextItem);
    }
    ImPlot::PushStyleVar(ImPlotStyleVar_FillAlpha, 0.5f);
    ImPlot::PlotShadedG(
      name, &PlotStacked, &bottomData, &PlotStacked, &topData, count);
    ImPlot::PopStyleVar();
  }
  if (mHideNextItem) {
    ImPlot::HideNextItem(*mHideNextItem);
  }
  ImPlot::PlotLineG(name, &PlotStacked, &topData, count);

  if (mKind == Kind::Lines || GetLastItemHidden()) {
    mStack.pop_back();
  }

  mHideNextItem = std::nullopt;
}

void ImStackedAreaPlotter::HideNextItem(ImPlotCond condition) {
  mHideNextItem = condition;
}