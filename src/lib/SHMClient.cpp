// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT

#include "SHMClient.hpp"
#include "SHM.hpp"
#include "CheckHResult.hpp"
#include <XRFrameTools/ABIKey.hpp>

static inline auto GetSHMPath() {
  return std::format(L"com.fredemmott.XRFrameTools/SHM/{}", ABIKey);
}

SHMClient::SHMClient() {
  mMapping.reset(CreateFileMappingW(
    INVALID_HANDLE_VALUE,
    nullptr,
    PAGE_READWRITE,
    0,
    sizeof(SHM),
    GetSHMPath().c_str()));

  if (!mMapping) {
    return;
  }

  mView.reset(MapViewOfFile(
    mMapping.get(), FILE_MAP_WRITE | FILE_MAP_READ, 0, 0, sizeof(SHM)));
  if (!mView) {
    ThrowHResult(HRESULT_FROM_WIN32(GetLastError()), "MapViewOfFile failed");
  }
}

SHMClient::~SHMClient() = default;

SHM* SHMClient::MaybeGetSHM() const noexcept {
  if (!mView) {
    return nullptr;
  }
  return static_cast<SHM*>(mView.get());
}
