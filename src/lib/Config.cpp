// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT

#include "Config.hpp"

#include <wil/registry.h>

#include <format>

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

void Config::OnRegistryChange(wil::RegistryChangeKind) {
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
  return wil::reg::open_unique_key(
    HKEY_CURRENT_USER,
    subkey.c_str(),
    access == Config::Access::ReadOnly ? wil::reg::key_access::read
                                       : wil::reg::key_access::readwrite);
}

Config Config::GetForOpenXRApp(
  Access access,
  const std::filesystem::path& path) {
  return {
    OpenKey(access, path.wstring()),
    OpenKey(access, DefaultsSubkey),
  };
}

Config Config::GetUserDefaults(Access access) {
  return {OpenKey(access, DefaultsSubkey), {}};
}

Config::Config(wil::unique_hkey appKey, wil::unique_hkey defaultsKey)
  : mAppKey(std::move(appKey)), mDefaultsKey(std::move(defaultsKey)) {
  this->Load();
}
