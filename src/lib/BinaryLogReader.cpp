// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT

#include "BinaryLogReader.hpp"

#include <wil/filesystem.h>

#include <magic_enum.hpp>

#include "BinaryLog.hpp"
#include "Win32Utils.hpp"

using OpenError = BinaryLogReader::OpenError;

OpenError::OpenError(Code code, decltype(mDetails)&& details)
  : mCode(code), mDetails(std::move(details)) {
}

OpenError OpenError::FailedToOpenFile(HRESULT result) {
  return {Code::FailedToOpenFile, result};
}

OpenError OpenError::BadMagic(
  const std::string& expected,
  const std::string& actual) {
  return {Code::BadMagic, std::tuple {expected, actual}};
}

OpenError OpenError::BadVersion(
  const std::string& expected,
  const std::string& actual) {
  return {Code::BadVersion, std::tuple {expected, actual}};
}

OpenError OpenError::BadBinaryHeader() {
  return {Code::BadBinaryHeader, {}};
}

OpenError OpenError::UnsupportedCompression(const std::string& actual) {
  return {Code::UnsupportedCompression, actual};
}

BinaryLogReader::BinaryLogReader(
  const std::filesystem::path& logFilePath,
  wil::unique_hfile file,
  const std::filesystem::path& executable,
  PerformanceCounterMath pcm,
  ClockCalibration cc)
  : mLogFilePath(logFilePath),
    mFile(std::move(file)),
    mExecutable(executable),
    mPerformanceCounterMath(pcm),
    mClockCalibration(cc) {
}

BinaryLogReader::~BinaryLogReader() = default;

BinaryLogReader::ClockCalibration BinaryLogReader::GetClockCalibration()
  const noexcept {
  return mClockCalibration;
}

std::filesystem::path BinaryLogReader::GetLogFilePath() const noexcept {
  return mLogFilePath;
}

PerformanceCounterMath BinaryLogReader::GetPerformanceCounterMath()
  const noexcept {
  return mPerformanceCounterMath;
}

std::optional<FramePerformanceCounters>
BinaryLogReader::GetNextFrame() noexcept {
  if (!mFile) {
    return std::nullopt;
  }

  auto& header = mNextPacketHeader;
  using Type = BinaryLog::PacketHeader::PacketType;
  if (header.mType == Type::Invalid) {
    DWORD bytesRead {};

    if (!ReadFile(
          mFile.get(),
          &header,
          sizeof(BinaryLog::PacketHeader),
          &bytesRead,
          nullptr)) {
      return std::nullopt;
    }
    if (bytesRead != sizeof(BinaryLog::PacketHeader)) {
      return std::nullopt;
    }
  }

  if (header.mType != Type::Core) {
    dprint("Unexpected packet type {}", std::to_underlying(header.mType));
    return std::nullopt;
  }

  enum class ErrorKind {
    WrongKind,
    WrongSize,
    ReadFailed,
    ReadPartiallyFailed,
  };
  using enum ErrorKind;

  const auto readPacket
    = [&]<class T>(const Type kind, T* dest) -> std::expected<void, ErrorKind> {
    if (header.mType != kind) {
      return std::unexpected {WrongKind};
    }
    if (header.mSize != sizeof(T)) {
      return std::unexpected {WrongSize};
    }

    DWORD bytesRead {};
    if (!ReadFile(mFile.get(), dest, sizeof(T), &bytesRead, 0)) {
      return std::unexpected {ReadFailed};
    }

    if (bytesRead != header.mSize) {
      return std::unexpected {ReadPartiallyFailed};
    }
    return {};
  };

  FramePerformanceCounters fpc;
  if (const auto it = readPacket(Type::Core, &fpc.mCore); !it.has_value()) {
    dprint(
      "Failed to read `core` packet: {}", magic_enum::enum_name(it.error()));
    return std::nullopt;
  }

  while (true) {
    header = {};
    DWORD bytesRead {};
    if (!ReadFile(
          mFile.get(),
          &header,
          sizeof(BinaryLog::PacketHeader),
          &bytesRead,
          nullptr)) {
      return fpc;
    }
    if (bytesRead != sizeof(BinaryLog::PacketHeader)) {
      return fpc;
    }

    switch (header.mType) {
      case Type::Invalid:
        dprint("Binary log contains packet with 'Invalid' type");
        return fpc;
      case Type::Core:
        return fpc;// next frame
      case Type::GpuTime:
        if (!readPacket(Type::GpuTime, &fpc.mRenderGpu)) {
          return fpc;
        }
        fpc.mValidDataBits |= FramePerformanceCounters::ValidDataBits::GpuTime;
        break;
      case Type::VRAM:
        if (!readPacket(Type::VRAM, &fpc.mVideoMemoryInfo)) {
          return fpc;
        }
        fpc.mValidDataBits |= FramePerformanceCounters::ValidDataBits::VRAM;
        break;
      case Type::NVAPI:
        if (!readPacket(Type::NVAPI, &fpc.mGpuPerformanceInformation)) {
          return fpc;
        }
        fpc.mValidDataBits |= FramePerformanceCounters::ValidDataBits::NVAPI;
        break;
      case Type::NVEncSession: {
        const auto i = fpc.mEncoders.mSessionCount++;
        if (!readPacket(Type::NVEncSession, &fpc.mEncoders.mSessions[i])) {
          --fpc.mEncoders.mSessionCount;
          return fpc;
        }
        fpc.mValidDataBits |= FramePerformanceCounters::ValidDataBits::NVEnc;
        break;
      }
    }
  }
}

std::filesystem::path BinaryLogReader::GetExecutablePath() const noexcept {
  return mExecutable;
}

std::expected<BinaryLogReader, BinaryLogReader::OpenError>
BinaryLogReader::Create(const std::filesystem::path& path) {
  auto [file, openError] = wil::try_open_file(path.wstring().c_str());

  if (!file) {
    return std::unexpected {OpenError::FailedToOpenFile(openError)};
  }

  const auto magic = ReadLine(file.get());
  if (magic != BinaryLog::Magic) {
    return std::unexpected {OpenError::BadMagic(BinaryLog::Magic, magic)};
  }

  const auto formatVersion = ReadLine(file.get());
  if (formatVersion != BinaryLog::GetVersionLine()) {
    return std::unexpected {
      OpenError::BadVersion(BinaryLog::GetVersionLine(), formatVersion)};
  }
  const auto producer = ReadLine(file.get());
  dprint("Reading binary log - {}", producer);

  const auto executableUtf8 = ReadLine(file.get());
  const auto executableFromUtf8
    = [&executableUtf8](wchar_t* buffer, const INT bufferSize) {
        return MultiByteToWideChar(
          CP_UTF8,
          MB_ERR_INVALID_CHARS,
          executableUtf8.c_str(),
          executableUtf8.size(),
          buffer,
          bufferSize);
      };
  const auto executableWideCharCount = executableFromUtf8(nullptr, 0);
  std::wstring executableWide;
  executableWide.resize(executableWideCharCount, L'\0');
  executableFromUtf8(executableWide.data(), executableWide.size());
  while ((!executableWide.empty()) && (executableWide.back() == L'\0')) {
    executableWide.pop_back();
  }
  const std::filesystem::path executable {executableWide};

  const auto compression = ReadLine(file.get());
  if (compression != "uncompressed") {
    return std::unexpected {OpenError::UnsupportedCompression(compression)};
  }

  using FileHeader = BinaryLog::FileHeader;
  char binaryHeaderData[sizeof(FileHeader)] {};
  DWORD bytesRead {};
  if (!ReadFile(
        file.get(),
        &binaryHeaderData,
        sizeof(FileHeader),
        &bytesRead,
        nullptr)) {
    return std::unexpected {OpenError::BadBinaryHeader()};
  }
  if (bytesRead != sizeof(FileHeader)) {
    return std::unexpected {OpenError::BadBinaryHeader()};
  }

  const auto binaryHeader
    = FileHeader::FromData(binaryHeaderData, sizeof(FileHeader));
  if (!(binaryHeader.mMicrosecondsSinceEpoch
        && binaryHeader.mQueryPerformanceFrequency.QuadPart
        && binaryHeader.mQueryPerformanceCounter.QuadPart)) {
    return std::unexpected {OpenError::BadBinaryHeader()};
  }

  return BinaryLogReader {
    path,
    std::move(file),
    executable,
    PerformanceCounterMath {binaryHeader.mQueryPerformanceFrequency},
    ClockCalibration {
      .mQueryPerformanceCounter = binaryHeader.mQueryPerformanceCounter,
      .mMicrosecondsSinceEpoch = binaryHeader.mMicrosecondsSinceEpoch,
    }};
}

std::string BinaryLogReader::ReadLine(HANDLE file) noexcept {
  // Byte-at-a-time so we don't overread and have to pass an offset to the
  // constructor
  std::string ret;
  while (ret.size() < 32 * 1024) {
    char byte {'\0'};
    DWORD bytesRead {};
    if (!(ReadFile(file, &byte, 1, &bytesRead, nullptr) && bytesRead)) {
      break;
    }
    ret += byte;
    if (byte == '\n') {
      break;
    }
  }

  if ((!ret.empty()) && ret.back() == '\n') {
    ret.pop_back();
    if ((!ret.empty()) && ret.back() == '\r') {
      ret.pop_back();
    }
  }
  return ret;
}
