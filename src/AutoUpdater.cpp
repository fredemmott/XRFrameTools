// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT

#include "AutoUpdater.hpp"

#include <wil/registry.h>
#include <wil/win32_helpers.h>

#include <filesystem>

#include "Config.hpp"
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

  const auto autoUpdatesSubkey
    = std::format(L"{}\\AutoUpdate", Config::RootSubkey);
  const auto enabled
    = wil::reg::try_get_value_dword(
        HKEY_CURRENT_USER, autoUpdatesSubkey.c_str(), L"Enabled")
        .value_or(1);
  if (!enabled) {
    dprint("Skipping auto-update due to registry setting");
  }

  const auto defaultChannel = Version::IsStableRelease ? L"live" : L"test";
  const auto registryChannel = wil::reg::try_get_value_string(
    HKEY_CURRENT_USER, autoUpdatesSubkey.c_str(), L"Channel");
  if (!registryChannel) {
    wil::reg::set_value_string_nothrow(
      HKEY_CURRENT_USER, autoUpdatesSubkey.c_str(), L"Channel", defaultChannel);
  }
  const auto channel = registryChannel.value_or(defaultChannel);

  auto commandLine = std::format(
    L"--channel=2/{} --local-version={} --silent", channel, Version::SemVerW);
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