// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT
#pragma once

#include "SHMClient.hpp"

#include <chrono>

class SHMReader final : public SHMClient {
 public:
  SHMReader();
  ~SHMReader();

  bool IsValid() const noexcept;

  // Throws std::logic_error if !IsValid()
  const SHM& GetSHM() const;

  // Throws std::logic_error if !IsValid()
  std::chrono::microseconds GetAge() const;

  inline auto operator->() const {
    return &GetSHM();
  }
};
