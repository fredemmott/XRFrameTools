// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT

#include "BinaryLogger.hpp"

#include <shlobj_core.h>
#include <wil/win32_helpers.h>

#include <chrono>
#include <filesystem>
#include <format>
#include <functional>

#include "BinaryLog.hpp"
#include "FramePerformanceCounters.hpp"

BinaryLogger::BinaryLogger() {
  mThread = std::jthread {std::bind_front(&BinaryLogger::Run, this)};
}

BinaryLogger::~BinaryLogger() = default;

void BinaryLogger::LogFrame(const FramePerformanceCounters& fpc) {
  {
    const std::unique_lock lock(mProducedMutex);
    const auto index = (mProduced++) % RingBufferSize;
    mRingBuffer.at(index) = fpc;
  }
  SetEvent(mWakeEvent.get());
}

void BinaryLogger::OpenFile() {
  const std::filesystem::path thisExe {wil::QueryFullProcessImageNameW().get()};

  wil::unique_cotaskmem_string localAppData;
  SHGetKnownFolderPath(
    FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, localAppData.put());
  const auto now = std::chrono::system_clock::now();
  const auto logPath = std::filesystem::path {localAppData.get()}
    / "XRFrameTools" / thisExe.stem()
    / std::format("{0} {1:%F} {1:%H-%M-%S} {1:%Z}.XRFrameToolsBinLog",
                  thisExe.stem().string(),
                  now);

  if (!std::filesystem::exists(logPath.parent_path())) {
    std::filesystem::create_directories(logPath.parent_path());
  }

  mFile.reset(CreateFileA(
    logPath.string().c_str(),
    GENERIC_WRITE,
    FILE_SHARE_READ,
    nullptr,
    CREATE_ALWAYS,
    FILE_ATTRIBUTE_NORMAL,
    nullptr));
  if (!mFile) {
    return;
  }

  const auto header = std::format(
    "{}\n{}\n{}\n",
    BinaryLog::Magic,
    BinaryLog::GetVersionLine(),
    thisExe.string());
  WriteFile(mFile.get(), header.data(), header.size(), nullptr, nullptr);
  LARGE_INTEGER pcf {};
  QueryPerformanceFrequency(&pcf);
  WriteFile(mFile.get(), &pcf, sizeof(pcf), nullptr, nullptr);
}

uint64_t BinaryLogger::GetProduced() {
  // Not certain an uint64_t read is atomic in the 32-bit builds, so let's
  // guard the read too
  std::unique_lock lock(mProducedMutex);
  return mProduced;
}

void BinaryLogger::Run(std::stop_token tok) {
  SetThreadDescription(GetCurrentThread(), L"XRFrameTools Binary Logger");
  OutputDebugStringA("XRFrameTools: starting binary logger thread");

  const std::stop_callback wakeOnStop(
    tok, std::bind_front(&SetEvent, mWakeEvent.get()));

  this->OpenFile();
  if (!mFile) {
    return;
  }

  const auto cleanup = wil::scope_exit([]() {
    OutputDebugStringA("XRFrameTools: shutting down binary logger thread");
  });

  while (WaitForSingleObject(mWakeEvent.get(), INFINITE) == WAIT_OBJECT_0) {
    if (tok.stop_requested()) {
      return;
    }

    const auto produced = this->GetProduced();
    if (produced == mConsumed) {
      continue;
    }

    const auto firstIndex = mConsumed % RingBufferSize;
    const auto lastIndex = (produced - 1) % RingBufferSize;

    constexpr auto entrySize = sizeof(mRingBuffer.front());

    const auto markConsumed
      = wil::scope_exit([this, produced]() { mConsumed = produced; });

    // WriteFile is cached - for now we're trusting that
    if (firstIndex <= lastIndex) {
      // Contiguous range of the ring buffer
      const auto& first = mRingBuffer.at(firstIndex);
      const auto count = (lastIndex - firstIndex) + 1;
      WriteFile(mFile.get(), &first, entrySize * count, nullptr, nullptr);
      continue;
    }

    // Write from firstIndex to the end of the ring buffer
    WriteFile(
      mFile.get(),
      &mRingBuffer.at(firstIndex),
      entrySize * (mRingBuffer.size() - firstIndex),
      nullptr,
      nullptr);

    // Write from start of the ringBuffer to lastIndex
    WriteFile(
      mFile.get(),
      &mRingBuffer.front(),
      entrySize * (lastIndex + 1),
      nullptr,
      nullptr);
  }
}
