// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT
#pragma once

#include <wil/resource.h>

#include <expected>
#include <filesystem>
#include <unordered_map>
#include <variant>

#include "BinaryLog.hpp"
#include "FramePerformanceCounters.hpp"
#include "PerformanceCounterMath.hpp"

class BinaryLogReader {
 public:
  BinaryLogReader() = delete;
  BinaryLogReader(BinaryLogReader&&) = default;
  ~BinaryLogReader();

  struct ClockCalibration {
    LARGE_INTEGER mQueryPerformanceCounter {};
    uint64_t mMicrosecondsSinceEpoch {};
  };

  class OpenError;

  [[nodiscard]] ClockCalibration GetClockCalibration() const noexcept;

  [[nodiscard]]
  std::filesystem::path GetLogFilePath() const noexcept;

  [[nodiscard]]
  PerformanceCounterMath GetPerformanceCounterMath() const noexcept;

  [[nodiscard]]
  std::filesystem::path GetExecutablePath() const noexcept;

  [[nodiscard]]
  DWORD GetProcessID() const noexcept;

  [[nodiscard]]
  std::optional<std::filesystem::path> GetExecutablePath(
    uint32_t pid) const noexcept;

  [[nodiscard]]
  uint64_t GetFileSize() const noexcept;

  /// File size, excluding the size of the header and footer
  [[nodiscard]]
  uint64_t GetStreamSize() const noexcept;

  [[nodiscard]]
  std::optional<BinaryLog::FileFooter> GetFileFooter() const noexcept;

  [[nodiscard]]
  BinaryLog::FileFooter GetOrComputeFileFooter() noexcept;

  [[nodiscard]]
  std::optional<FramePerformanceCounters> GetNextFrame() noexcept;

  [[nodiscard]]
  static std::expected<BinaryLogReader, OpenError> Create(
    const std::filesystem::path& path);

  class OpenError {
   public:
    enum class Code {
      FailedToOpenFile,
      BadMagic,
      BadVersion,
      BadBinaryHeader,
      UnsupportedCompression,
    };

    [[nodiscard]]
    Code GetCode() const noexcept {
      return mCode;
    }

    static OpenError FailedToOpenFile(HRESULT result);
    static OpenError BadMagic(
      const std::string& expected,
      const std::string& actual);
    static OpenError BadVersion(
      const std::string& expected,
      const std::string& actual);
    static OpenError UnsupportedCompression(const std::string& actual);
    static OpenError BadBinaryHeader();

   private:
    Code mCode;
    std::variant<
      std::monostate,
      HRESULT,
      std::string,
      std::tuple<std::string, std::string>>
      mDetails;

    OpenError() = delete;
    OpenError(Code code, decltype(mDetails)&& details);
  };

 private:
  std::filesystem::path mLogFilePath;
  wil::unique_hfile mFile;
  std::filesystem::path mExecutable;
  uint32_t mProcessID;
  PerformanceCounterMath mPerformanceCounterMath;
  ClockCalibration mClockCalibration {};
  std::unordered_map<uint32_t, std::filesystem::path> mProcesses;

  uint64_t mFileSize {};
  uint64_t mStreamSize {};// File size, excluding header and footer
  std::optional<BinaryLog::FileFooter> mFooter {};

  BinaryLog::FileFooter mComputedFooter {};
  BinaryLog::PacketHeader mNextPacketHeader {};
  bool mEndOfFile {false};

  BinaryLogReader(
    const std::filesystem::path& path,
    wil::unique_hfile,
    const std::filesystem::path& executable,
    uint32_t processID,
    PerformanceCounterMath,
    ClockCalibration);

  static std::string ReadLine(HANDLE) noexcept;
};