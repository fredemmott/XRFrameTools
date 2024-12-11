// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT
#pragma once

#include <Windows.h>

#include <cinttypes>
#include <filesystem>
#include <functional>
#include <optional>
#include <shared_mutex>

// after <optional>
#include <wil/registry.h>

#define XRFT_ITERATE_SETTINGS(X) \
  X(int64_t, BinaryLoggingEnabledUntil, BinaryLoggingDisabled)

class Config {
 public:
  static constexpr auto RootSubkey = LR"(Software\Fred Emmott\XRFrameTools)";
  enum class Access {
    ReadOnly,
    ReadWrite,
  };

  Config() = delete;
  static inline Config GetForOpenXRApp(Access, const std::filesystem::path&);
  static inline Config GetUserDefaults(Access);

  static constexpr int64_t BinaryLoggingDisabled = 0;
  static constexpr int64_t BinaryLoggingPermanentlyEnabled = -1;

#define DEFINE_GETTER(TYPE, NAME, DEFAULT) \
  inline TYPE NAME() const noexcept { \
    std::shared_lock lock(const_cast<std::shared_mutex&>(mMutex)); \
    if (mAppStorage.m##NAME.has_value()) { \
      return mAppStorage.m##NAME.value(); \
    } \
    return mDefaultsStorage.m##NAME.value_or(DEFAULT); \
  }
#define DECLARE_SETTER(TYPE, NAME, DEFAULT) \
  void Set##NAME(const TYPE& NAME) noexcept;
  XRFT_ITERATE_SETTINGS(DEFINE_GETTER)
  XRFT_ITERATE_SETTINGS(DECLARE_SETTER)
#undef DEFINE_GETTER
 private:
  static constexpr auto DefaultsSubkey = L"__defaults__";
  Config(wil::unique_hkey appKey, wil::unique_hkey defaultsKey);

  std::shared_mutex mMutex;

  struct Storage {
#define DECLARE_SETTING_STORAGE(TYPE, NAME, DEFAULT) \
  std::optional<TYPE> m##NAME {DEFAULT};
    XRFT_ITERATE_SETTINGS(DECLARE_SETTING_STORAGE)
#undef DECLARE_SETTING_STORAGE
  };
  Storage mDefaultsStorage {};
  Storage mAppStorage {};

  wil::unique_hkey mDefaultsKey;
  wil::unique_hkey mAppKey;

  void Load();
  static void Load(Storage& storage, HKEY hkey);

  const wil::unique_registry_watcher_nothrow mWatcher
    = wil::make_registry_watcher_nothrow(
      HKEY_CURRENT_USER,
      RootSubkey,
      /* recursive = */ true,
      std::bind_front(&Config::OnRegistryChange, this));

  void OnRegistryChange(wil::RegistryChangeKind);
};