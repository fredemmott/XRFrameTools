// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT

#include "Win32Utils.hpp"

#include <shlobj_core.h>
#include <wil/resource.h>

std::filesystem::path GetKnownFolderPath(const GUID& folderID) {
  wil::unique_cotaskmem_string buffer;
  SHGetKnownFolderPath(folderID, KF_FLAG_DEFAULT, nullptr, buffer.put());
  return std::filesystem::path {buffer.get()};
}
