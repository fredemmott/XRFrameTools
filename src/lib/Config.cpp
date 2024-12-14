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

std::expected<wil::unique_hkey, HRESULT>
create_unique_wow6464_hkey(HKEY key, PCWSTR subkey, DWORD accessBits) {
  accessBits |= KEY_WOW64_64KEY;

  wil::unique_hkey ret;
  RegCreateKeyExW(
    key, subkey, 0, nullptr, 0, accessBits, nullptr, ret.put(), nullptr);
  if (ret) {
    return ret;
  }
  return std::unexpected {HRESULT_FROM_WIN32(GetLastError())};
}

std::expected<wil::unique_hkey, HRESULT> create_unique_wow6464_hkey(
  HKEY key,
  PCWSTR subkey,
  wil::reg::key_access access = wil::reg::key_access::read) {
  DWORD accessBits {};
  switch (access) {
    case wil::reg::key_access::read:
      accessBits |= KEY_READ;
      break;
    case wil::reg::key_access::readwrite:
      accessBits |= KEY_ALL_ACCESS;
      break;
  }
  return create_unique_wow6464_hkey(key, subkey, accessBits);
}

std::expected<wil::unique_hkey, HRESULT> create_app_key(
  Config::Access access,
  std::wstring_view appName) {
  const auto subkey = std::format(L"{}\\Apps\\{}", Config::RootSubkey, appName);
  return create_unique_wow6464_hkey(
    HKEY_CURRENT_USER,
    subkey.c_str(),
    access == Config::Access::ReadOnly ? wil::reg::key_access::read
                                       : wil::reg::key_access::readwrite);
}

template <class T>
struct ConfigRegistryValue {
  static auto Read(HKEY key, LPCWSTR name) noexcept {
    return wil::reg::try_get_value<T>(key, name);
  }

  static void Write(HKEY key, LPCWSTR name, T value) {
    wil::reg::set_value<T>(key, name, value);
  }
};

template <class T>
void Set(
  std::shared_mutex& mutex,
  HKEY hkey,
  LPCWSTR name,
  std::optional<T>& storage,
  const T& value) noexcept {
  std::unique_lock lock(mutex);
  ConfigRegistryValue<T>::Write(hkey, name, value);
  storage.emplace(value);
}
}// namespace

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

Config Config::GetForOpenXRApp(
  Access access,
  const std::filesystem::path& path) {
  // use `generic_wstring()` to get a forward-slash-separated path,
  // so it is taken as a single key name rather than a nested key name
  return {
    create_app_key(access, path.generic_wstring())
      .value_or(wil::unique_hkey {}),
    create_app_key(access, DefaultsSubkey).value_or(wil::unique_hkey {}),
  };
}

Config Config::GetUserDefaults(Access access) {
  return {
    create_app_key(access, DefaultsSubkey).value_or(wil::unique_hkey {}), {}};
}

Config Config::GetForOpenXRAPILayer() {
  static const auto thisExe = std::filesystem::canonical(
    std::filesystem::path {wil::QueryFullProcessImageNameW().get()});
  if (
    auto appKey
    = create_app_key(Access::ReadWrite, thisExe.generic_wstring().c_str())) {
    wil::reg::set_value_qword_nothrow(
      std::move(appKey).value().get(),
      L"LastSeen",
      std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch())
        .count());
  }

  return GetForOpenXRApp(Access::ReadOnly, thisExe);
}

Config::Config(wil::unique_hkey appKey, wil::unique_hkey defaultsKey)
  : mAppKey(std::move(appKey)), mDefaultsKey(std::move(defaultsKey)) {
  if (
    auto watchKey
    = create_unique_wow6464_hkey(HKEY_CURRENT_USER, RootSubkey, KEY_NOTIFY)) {
    mWatcher = wil::make_registry_watcher_nothrow(
      std::move(watchKey).value(),
      /* recursive = */ true,
      std::bind_front(&Config::OnRegistryChange, this));
  }

  this->Load();
}