// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT
#pragma once

#include <wil/resource.h>

#include <expected>
#include <filesystem>
#include <variant>

#include "FramePerformanceCounters.hpp"
#include "PerformanceCounterMath.hpp"

class BinaryLogReader {
 public:
  BinaryLogReader() = delete;
  BinaryLogReader(BinaryLogReader&&) = default;
  ~BinaryLogReader();

  class OpenError;

  [[nodiscard]]
  std::filesystem::path GetLogFilePath() const noexcept;

  [[nodiscard]]
  PerformanceCounterMath GetPerformanceCounterMath() const noexcept;

  [[nodiscard]]
  std::filesystem::path GetExecutablePath() const noexcept;

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
  PerformanceCounterMath mPerformanceCounterMath;

  BinaryLogReader(
    const std::filesystem::path& path,
    wil::unique_hfile,
    const std::filesystem::path& executable,
    PerformanceCounterMath);

  static std::string ReadLine(HANDLE) noexcept;
};