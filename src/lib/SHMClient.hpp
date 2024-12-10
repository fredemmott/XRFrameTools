// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT
#pragma once

struct SHM;

#include <Windows.h>
#include <wil/resource.h>

class SHMClient {
  SHMClient(const SHMClient&) = delete;
  SHMClient(SHMClient&&) = delete;
  SHMClient& operator=(const SHMClient&) = delete;
  SHMClient& operator=(SHMClient&&) = delete;

 protected:
  SHMClient();
  ~SHMClient();

  SHM* MaybeGetSHM() const noexcept;

 private:
  wil::unique_hfile mMapping;
  wil::unique_any<void*, decltype(&::UnmapViewOfFile), &UnmapViewOfFile> mView;
};
