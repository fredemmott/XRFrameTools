// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT
#pragma once

#include <Windows.h>

#include <cinttypes>
#include <expected>
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
  static Config GetForOpenXRApp(Access, const std::filesystem::path&);
  static Config GetUserDefaults(Access);
  //// GetForOpenXRApp(Access::ReadOnly, <current executable path>)
  static Config GetForOpenXRAPILayer();

  static constexpr int64_t BinaryLoggingDisabled = 0;
  static constexpr int64_t BinaryLoggingPermanentlyEnabled = -1;

#define DEFINE_GETTER(TYPE, NAME, DEFAULT) \
  inline TYPE Get##NAME() const noexcept { \
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

  bool IsBinaryLoggingEnabled() const noexcept {
    switch (const auto value = this->GetBinaryLoggingEnabledUntil()) {
      case BinaryLoggingDisabled:
        return false;
      case BinaryLoggingPermanentlyEnabled:
        return true;
      default:
        return value > std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
    }
  }

 private:
  static constexpr auto DefaultsSubkey = L"__defaults__";

  Config(wil::unique_hkey appKey, wil::unique_hkey defaultsKey);

  std::shared_mutex mMutex;

  struct Storage {
#define DECLARE_SETTING_STORAGE(TYPE, NAME, DEFAULT) \
  std::optional<TYPE> m##NAME {std::nullopt};
    XRFT_ITERATE_SETTINGS(DECLARE_SETTING_STORAGE)
#undef DECLARE_SETTING_STORAGE
  };
  Storage mDefaultsStorage {};
  Storage mAppStorage {};

  wil::unique_hkey mDefaultsKey;
  wil::unique_hkey mAppKey;
  wil::unique_registry_watcher_nothrow mWatcher;

  void Load();
  static void Load(Storage& storage, HKEY hkey);

  void OnRegistryChange(wil::RegistryChangeKind);
};