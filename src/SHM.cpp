// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT

#include "SHM.hpp"

#include <memoryapi.h>

#include <XRFrameTools/ABIKey.hpp>

#include "CheckHResult.hpp"
#include "PerformanceCountersToDuration.hpp"

static auto GetSHMPath() {
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

SHMWriter::SHMWriter() {
  const auto shm = MaybeGetSHM();
  if (!shm) {
    return;
  }

  if (InterlockedIncrement64(&shm->mWriterCount) == 1) {
    shm->mFrameCount = 0;
  }
}

SHMWriter::~SHMWriter() {
  const auto shm = MaybeGetSHM();
  if (!shm) {
    return;
  }
  InterlockedDecrement64(&shm->mWriterCount);
}

void SHMWriter::LogFrame(const FrameMetrics& metrics) const {
  const auto shm = MaybeGetSHM();
  if (!shm) {
    return;
  }

  if (shm->mWriterCount > 1) {
    return;
  }
  shm->mFrameMetrics.at(shm->mFrameCount % SHM::MaxFrameCount) = metrics;
  ++shm->mFrameCount;
  QueryPerformanceCounter(&shm->mLastUpdate);
}

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

std::chrono::microseconds SHM::GetAge() const noexcept {
  LARGE_INTEGER now {};
  QueryPerformanceCounter(&now);
  return PerformanceCountersToDuration(now.QuadPart - mLastUpdate.QuadPart);
}