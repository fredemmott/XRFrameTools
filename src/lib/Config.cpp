// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT

#include "Config.hpp"

#include <wil/registry.h>
#include <wil/win32_helpers.h>

#include <filesystem>
#include <format>

#include "Win32Utils.hpp"

using namespace std::string_view_literals;

namespace {
template <class T>
struct ConfigRegistryValue {
  static auto Read(HKEY key, LPCWSTR name) noexcept {
    return wil::reg::try_get_value<T>(key, name);
  }

  static void Write(HKEY key, LPCWSTR name, T value) {
    wil::reg::set_value<T>(key, name, value);
  }
};
}// namespace

template <class T>
static void Set(
  std::shared_mutex& mutex,
  HKEY hkey,
  LPCWSTR name,
  std::optional<T>& storage,
  const T& value) noexcept {
  std::unique_lock lock(mutex);
  ConfigRegistryValue<T>::Write(hkey, name, value);
  storage.emplace(value);
}

#define DEFINE_SETTER(TYPE, NAME, DEFAULT) \
  void Config::Set##NAME(const TYPE& value) noexcept { \
    Set<TYPE>(mMutex, mAppKey.get(), L#NAME, mAppStorage.m##NAME, value); \
  }
XRFT_ITERATE_SETTINGS(DEFINE_SETTER)
#undef DEFINE_SETTER

void Config::OnRegistryChange(wil::RegistryChangeKind) {
  dprint("Registry settings changed");
  this->Load();
}

void Config::Load() {
  std::unique_lock lock(mMutex);

  Load(mDefaultsStorage, mDefaultsKey.get());
  Load(mAppStorage, mAppKey.get());
}

void Config::Load(Storage& storage, HKEY key) {
  if (!key) {
    return;
  }
  storage = {};

#define READ_REGISTRY_VALUE(TYPE, NAME, DEFAULT) \
  if (auto value = ConfigRegistryValue<TYPE>::Read(key, L#NAME)) { \
    storage.m##NAME = *std::move(value); \
  }
  XRFT_ITERATE_SETTINGS(READ_REGISTRY_VALUE)
#undef READ_REGISTRY_VALUE
}

static auto OpenKey(Config::Access access, std::wstring_view subpath) {
  const auto subkey = std::format(L"{}\\Apps\\{}", Config::RootSubkey, subpath);
  return wil::reg::create_unique_key(
    HKEY_CURRENT_USER,
    subkey.c_str(),
    access == Config::Access::ReadOnly ? wil::reg::key_access::read
                                       : wil::reg::key_access::readwrite);
}

Config Config::GetForOpenXRApp(
  Access access,
  const std::filesystem::path& path) {
  // use `generic_wstring()` to get a forward-slash-separated path,
  // so it is taken as a single key name rather than a nested key name
  return {
    OpenKey(access, path.generic_wstring()),
    OpenKey(access, DefaultsSubkey),
  };
}

Config Config::GetUserDefaults(Access access) {
  return {OpenKey(access, DefaultsSubkey), {}};
}

Config Config::GetForOpenXRAPILayer() {
  static const auto thisExe = std::filesystem::canonical(
    std::filesystem::path {wil::QueryFullProcessImageNameW().get()});

  wil::reg::set_value_qword_nothrow(
    HKEY_CURRENT_USER,
    std::format(LR"({}\Apps\{})", RootSubkey, thisExe.generic_wstring())
      .c_str(),
    L"LastSeen",
    std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::system_clock::now().time_since_epoch())
      .count());
  return GetForOpenXRApp(Access::ReadOnly, thisExe);
}

Config::Config(wil::unique_hkey appKey, wil::unique_hkey defaultsKey)
  : mAppKey(std::move(appKey)), mDefaultsKey(std::move(defaultsKey)) {
  this->Load();
}
