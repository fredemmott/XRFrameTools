// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT

#include "BinaryLogReader.hpp"

#include <wil/filesystem.h>

#include "BinaryLog.hpp"

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

OpenError OpenError::BadPerformanceCounterFrequency() {
  return {Code::BadPerformanceCounterFrequency, {}};
}

BinaryLogReader::BinaryLogReader(
  const std::filesystem::path& logFilePath,
  wil::unique_hfile file,
  const std::filesystem::path& executable,
  PerformanceCounterMath pcm)
  : mLogFilePath(logFilePath),
    mFile(std::move(file)),
    mExecutable(executable),
    mPerformanceCounterMath(pcm) {
}

BinaryLogReader::~BinaryLogReader() = default;
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
  FramePerformanceCounters fpc;
  DWORD bytesRead {};
  if (!ReadFile(mFile.get(), &fpc, sizeof(fpc), &bytesRead, 0)) {
    return std::nullopt;
  }
  if (bytesRead != sizeof(fpc)) {
    return std::nullopt;
  }

  return fpc;
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

  const auto version = ReadLine(file.get());
  if (version != BinaryLog::GetVersionLine()) {
    return std::unexpected {
      OpenError::BadVersion(BinaryLog::GetVersionLine(), version)};
  }

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

  LARGE_INTEGER performanceCounterFrequency {};
  DWORD bytesRead {};
  if (!ReadFile(
        file.get(),
        &performanceCounterFrequency,
        sizeof(performanceCounterFrequency),
        &bytesRead,
        nullptr)) {
    return std::unexpected {OpenError::BadPerformanceCounterFrequency()};
  }
  if (bytesRead != sizeof(performanceCounterFrequency)) {
    return std::unexpected {OpenError::BadPerformanceCounterFrequency()};
  }

  return BinaryLogReader {
    path,
    std::move(file),
    executable,
    PerformanceCounterMath {performanceCounterFrequency},
  };
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
