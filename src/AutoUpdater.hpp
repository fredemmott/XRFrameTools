// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT
#pragma once

#include <wil/resource.h>
#include <chrono>

class AutoUpdater {
 public:
  AutoUpdater();
  ~AutoUpdater();

  void GiveFocusIfRunning();

 private:
  wil::unique_handle mProcess;
  wil::unique_handle mThread;
  DWORD mThreadId {};
  std::chrono::steady_clock::time_point mLastGiveFocus;
};