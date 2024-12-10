// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT

#include "SHMWriter.hpp"

#include "SHM.hpp"

SHMWriter::SHMWriter() {
  const auto shm = MaybeGetSHM();
  if (!shm) {
    return;
  }

  if (InterlockedIncrement64(&shm->mWriterCount) == 1) {
    shm->mFrameCount = 0;
    shm->mWriterProcessID = GetCurrentProcessId();
  }
}

SHMWriter::~SHMWriter() {
  const auto shm = MaybeGetSHM();
  if (!shm) {
    return;
  }
  InterlockedDecrement64(&shm->mWriterCount);
}

void SHMWriter::LogFrame(const FramePerformanceCounters& metrics) const {
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
