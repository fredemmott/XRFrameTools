// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT
#pragma once

#include <Windows.h>
#include <filesystem>

std::filesystem::path GetKnownFolderPath(const GUID& folderID);