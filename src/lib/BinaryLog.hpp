// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT
#pragma once

#include <chrono>
#include <format>
#include <stdexcept>

#include "FramePerformanceCounters.hpp"

namespace BinaryLog {
/* Binary Log Format
 * =================
 *
 * The binary log contains:
 * 1. a human-readable header
 * 2. a `LARGE_INTEGER` (int64_t) containing the result of
 *   `QueryPerformanceFrequency()`
 * 3. a `LARGE_INTEGER` (int64_t) containing the result of
 *   `QueryPerformanceCounter()`
 * 4. a `uint64_t` containing the number of microseconds since
 *   1970-01-01 00:00:00Z.
 * 5. a contiguous stream of `FramePerformanceCounter` structs
 *
 * There is no separator between sections or between
 * `FramePerformanceCounter` structs.
 *
 * The `QueryPerformanceCounter()` and `uint64_t` timestamps can be used by
 * readers to convert `FramePerformanceCounter` values to human-readable times;
 * writers SHOULD aim to produce these values at same moment, or *immediately*
 * after each other.
 *
 * Human-Readable Header
 * ---------------------
 *
 * - The header MUST be encoded in UTF-8 without BOM; this primarily affects
 *   the executable path
 * - Writers MUST end all lines with a single LF character, not CRLF
 * - Writers MUST NOT omit the final newline
 *
 * Format:
 * ```
 * MAGIC\n
 * FORMAT_VERSION_LINE\n
 * Produced by: HUMAN_READABLE_APP_NAME_AND_VERSION\n
 * FULL_PATH_TO_EXECUTABLE\n
 * uncompressed\n
 * ```
 *
 * HUMAN_READABLE_APP_NAME_AND_VERSION should not be parsed or validated by
 * any readers - it is purely for debugging
 */
static constexpr auto Version = "2024-12-19#01";
static constexpr auto Magic = "XRFrameTools binary log";

inline auto GetVersionLine() noexcept {
  return std::format(
    "BLv{}/FPCv{}", BinaryLog::Version, FramePerformanceCounters::Version);
}

struct BinaryHeader {
  LARGE_INTEGER mQPFrequency {};
  LARGE_INTEGER mQPCounter {};
  uint64_t mSinceEpochInMicros {};

  static BinaryHeader Now() {
    BinaryHeader ret {};
    QueryPerformanceFrequency(&ret.mQPFrequency);
    QueryPerformanceFrequency(&ret.mQPCounter);

    // Check that `std::chrono::system_clock` is guaranteed to use the Unix
    // epoch
    static_assert(__cpp_lib_chrono >= 201907L);
    ret.mSinceEpochInMicros
      = duration_cast<std::chrono::microseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count();
    return ret;
  }

  static BinaryHeader FromData(const void* data, const std::size_t size) {
    if (size != sizeof(BinaryHeader)) [[unlikely]] {
      throw std::logic_error(
        "Calling BinaryHeader::FromData with incorrect size");
    }
    BinaryHeader ret {};
    memcpy(&ret, data, size);
    return ret;
  }

 private:
  BinaryHeader() = default;
};
// Assert it's the same size in all builds, especially 32- vs 64-bit
static_assert(sizeof(BinaryHeader) == 24);
};// namespace BinaryLog