// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT

#include "BinaryLogWriter.hpp"

#include <shlobj_core.h>
#include <wil/win32_helpers.h>

#include <chrono>
#include <filesystem>
#include <format>
#include <functional>

#include "BinaryLog.hpp"
#include "FramePerformanceCounters.hpp"
#include "Version.hpp"
#include "Win32Utils.hpp"

BinaryLogWriter::BinaryLogWriter() {
  mThread = std::jthread {std::bind_front(&BinaryLogWriter::Run, this)};
}

BinaryLogWriter::~BinaryLogWriter() {
  mThread = {};
  if (!mFile) {
    return;
  }
  using namespace BinaryLog;
  constexpr PacketHeader header {
    PacketHeader::PacketType::FileFooter,
    sizeof(FileFooter),
  };
  WriteFile(mFile.get(), &header, sizeof(header), nullptr, nullptr);
  WriteFile(mFile.get(), &mFooter, sizeof(mFooter), nullptr, nullptr);
  WriteFile(
    mFile.get(),
    &FileFooter::TrailingMagic,
    std::size(FileFooter::TrailingMagic),
    nullptr,
    nullptr);
}

void BinaryLogWriter::LogFrame(const FramePerformanceCounters& fpc) {
  {
    const std::unique_lock lock(mProducedMutex);
    const auto index = (mProduced++) % RingBufferSize;
    mRingBuffer.at(index) = fpc;
  }
  SetEvent(mWakeEvent.get());
}

void BinaryLogWriter::OpenFile() {
  const std::filesystem::path thisExe {wil::QueryFullProcessImageNameW().get()};

  const auto thisExeToUtf8
    = [wide = thisExe.wstring()](char* buffer, const INT bufferSize) {
        return WideCharToMultiByte(
          CP_UTF8,
          WC_ERR_INVALID_CHARS,
          wide.data(),
          wide.size(),
          buffer,
          bufferSize,
          nullptr,
          nullptr);
      };
  const auto thisExeUtf8Bytes = thisExeToUtf8(nullptr, 0);
  if (thisExeUtf8Bytes <= 0) {
    return;
  }
  std::string thisExeUtf8;
  thisExeUtf8.resize(static_cast<size_t>(thisExeUtf8Bytes), '\0');
  if (thisExeToUtf8(thisExeUtf8.data(), thisExeUtf8.size()) <= 0) {
    return;
  }
  {
    const auto lastIdx = thisExeUtf8.find_last_not_of('\0');
    if (lastIdx == std::string::npos) {
      return;
    }
    thisExeUtf8.erase(lastIdx + 1);
  }

  const auto now = std::chrono::system_clock::now();
  const auto logPath = GetKnownFolderPath(FOLDERID_LocalAppData)
    / L"XRFrameTools" / "Logs" / thisExe.stem()
    / std::format(L"{0} {1:%F} {1:%H-%M-%S} {1:%Z}.XRFTBinLog",
                  thisExe.stem().wstring(),
                  now);

  try {
    if (!std::filesystem::exists(logPath.parent_path())) {
      std::filesystem::create_directories(logPath.parent_path());
    }
  } catch (const std::filesystem::filesystem_error& e) {
    dprint("failed to create log file direction: {}", e.what());
    return;
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

  const auto textHeader = std::format(
    "{}\n{}\nProduced by: {} v{}\n{}\nuncompressed\n",
    BinaryLog::Magic,
    BinaryLog::GetVersionLine(),
    Version::ProjectName,
    Version::SemVer,
    thisExeUtf8);

  WriteFile(
    mFile.get(), textHeader.data(), textHeader.size(), nullptr, nullptr);

  const auto binaryHeader = BinaryLog::FileHeader::Now();
  WriteFile(mFile.get(), &binaryHeader, sizeof(binaryHeader), nullptr, nullptr);
}

uint64_t BinaryLogWriter::GetProduced() {
  // Not certain an uint64_t read is atomic in the 32-bit builds, so let's
  // guard the read too
  std::unique_lock lock(mProducedMutex);
  return mProduced;
}

void BinaryLogWriter::Run(std::stop_token tok) {
  SetThreadDescription(GetCurrentThread(), L"XRFrameTools Binary Logger");
  dprint("starting binary logger thread");

  const std::stop_callback wakeOnStop(
    tok, std::bind_front(&SetEvent, mWakeEvent.get()));

  this->OpenFile();
  if (!mFile) {
    return;
  }

  const auto cleanup
    = wil::scope_exit([]() { dprint("shutting down binary logger thread"); });

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

    using FPC = FramePerformanceCounters;

    static char buf[sizeof(FPC)];
    size_t offset {0};
    const auto appendBlob = [buf = &buf, &offset]<class T>(const T& data) {
      memcpy_s(&(*buf)[offset], sizeof(*buf) - offset, &data, sizeof(T));
      offset += sizeof(data);
    };

    for (uint64_t i = mConsumed; i < produced; ++i) {
      const auto& it = mRingBuffer.at(i % RingBufferSize);

      mFooter.Update(it);

      using PH = BinaryLog::PacketHeader;
      using PT = PH::PacketType;

      const auto appendPacket
        = [appendBlob, activeBits = it.mValidDataBits]<class T>(
            const PT kind,
            const T& payload,
            const FPC::ValidDataBits neededBits = {}) {
            if ((activeBits & neededBits) != neededBits) {
              return;
            }

            appendBlob(PH {kind, sizeof(T)});
            appendBlob(payload);
          };

      appendPacket(PT::Core, it.mCore);
      appendPacket(PT::GpuTime, it.mRenderGpu, FPC::ValidDataBits::GpuTime);
      appendPacket(PT::VRAM, it.mVideoMemoryInfo, FPC::ValidDataBits::VRAM);
      appendPacket(
        PT::NVAPI, it.mGpuPerformanceInformation, FPC::ValidDataBits::NVAPI);

      if (it.mValidDataBits & std::to_underlying(FPC::ValidDataBits::NVEnc)) {
        for (int j = 0; j < it.mEncoders.mSessionCount; ++j) {
          appendPacket(PT::NVEncSession, it.mEncoders.mSessions.at(i));
        }
      }
    }

    if (offset == 0) {
      continue;
    }

    WriteFile(mFile.get(), buf, static_cast<DWORD>(offset), nullptr, nullptr);
  }
}
