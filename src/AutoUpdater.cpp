// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT

#include "AutoUpdater.hpp"

#include <wil/registry.h>
#include <wil/win32_helpers.h>
#include <wil/windowing.h>

#include <filesystem>

#include "Version.hpp"
#include "Win32Utils.hpp"

AutoUpdater::AutoUpdater() {
  const auto updater
    = std::filesystem::path {wil::QueryFullProcessImageNameW().get()}
        .parent_path()
    / "fredemmott_XRFrameTools_Updater.exe";
  if (!std::filesystem::exists(updater)) {
    dprint("Auto-updater has been deleted, not invoking");
    return;
  }

  const auto channel = Version::IsStableRelease ? L"live" : L"test";

  auto commandLine = std::format(
    L"--channel={} --local-version={} --silent", channel, Version::SemVerW);
  STARTUPINFOW startupInfo {sizeof(STARTUPINFOW)};
  PROCESS_INFORMATION processInfo {};

  if (!CreateProcessW(
        updater.wstring().c_str(),
        commandLine.data(),
        nullptr,
        nullptr,
        FALSE,
        0,
        nullptr,
        nullptr,
        &startupInfo,
        &processInfo)) {
    dprint(
      "⚠️ failed to launch updater: {:#010x}",
      HRESULT_FROM_WIN32(GetLastError()));
    return;
  }
  mProcess.reset(processInfo.hProcess);
  mThread.reset(processInfo.hThread);
  mThreadId = processInfo.dwThreadId;
  dprint("Started updater with process {}", processInfo.dwProcessId);
}

AutoUpdater::~AutoUpdater() = default;