// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT
#pragma once

#include "BinaryLogReader.hpp"

namespace CSVWriter {
static constexpr size_t DefaultFramesPerRow = 10;

struct Result {
  size_t mFrameCount {};
  size_t mRowCount {};
  std::optional<std::chrono::milliseconds> mLogDuration {};
};

/** Write to CSV
 *
 * May throw `std::system_error`; you might want to specially handle
 * `std::filesystem::filesystem_error`
 */
Result Write(
  BinaryLogReader reader,
  const std::filesystem::path& outputPath,
  size_t framesPerRow);

/** Write to CSV
 *
 * May throw `std::system_error`; you might want to specially handle
 * `std::filesystem::filesystem_error`
 */
Result Write(BinaryLogReader reader, HANDLE outputFile, size_t framesPerRow);
}// namespace CSVWriter
