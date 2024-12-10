// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT

#include "SHMReader.hpp"
#include "PerformanceCountersToDuration.hpp"
#include "SHM.hpp"

SHMReader::SHMReader() = default;
SHMReader::~SHMReader() = default;

bool SHMReader::IsValid() const noexcept {
  return static_cast<bool>(MaybeGetSHM());
}

const SHM& SHMReader::GetSHM() const {
  const auto shm = MaybeGetSHM();
  if (!shm) [[unlikely]] {
    throw std::logic_error("Calling GetSHM() without checking IsValid()");
  }
  return *shm;
}

std::chrono::microseconds SHMReader::GetAge() const noexcept {
  const auto shm = MaybeGetSHM();
  if (!shm) [[unlikely]] {
    throw std::logic_error("Calling GetSHM() without checking IsValid()");
  }
  LARGE_INTEGER now {};
  QueryPerformanceCounter(&now);
  return PerformanceCountersToDuration(now.QuadPart - shm->mLastUpdate.QuadPart);
}
