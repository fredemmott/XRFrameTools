// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT

#include "FrameMetricsStore.hpp"

#include <ranges>

FrameMetricsStore::Frame& FrameMetricsStore::GetForWaitFrame() noexcept {
  return mTrackedFrames.at(mWaitFrameCount++ % mTrackedFrames.size());
}

FrameMetricsStore::Frame& FrameMetricsStore::GetForBeginFrame() noexcept {
  const auto it = std::ranges::find_if(mTrackedFrames, [](Frame& it) {
    bool canBegin {true};
    return it.mCanBegin.compare_exchange_strong(canBegin, false);
  });

  if (it == mTrackedFrames.end()) {
    auto& ret
      = mUntrackedFrames.at(mUntrackedFrameCount++ % mTrackedFrames.size());
    ret.Reset();
    return ret;
  }

  return *it;
}

FrameMetricsStore::Frame& FrameMetricsStore::GetForEndFrame(
  uint64_t displayTime) noexcept {
  const auto it
    = std::ranges::find(mTrackedFrames, displayTime, &Frame::mDisplayTime);
  if (it == mTrackedFrames.end()) {
    auto& ret
      = mUntrackedFrames.at(mUntrackedFrameCount++ % mTrackedFrames.size());
    ret.Reset();
    return ret;
  }

  return *it;
}