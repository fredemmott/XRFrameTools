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
 * 5. a contiguous stream of `PacketHeader` structs followed by a
 *   variable-length packet data
 *
 * There is no separator between sections or between
 * packets.
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
static constexpr auto Version = "2025-06-04#01";
static constexpr auto Magic = "XRFrameTools binary log";

inline auto GetVersionLine() noexcept {
  return std::format(
    "BLv{}/FPCv{}", BinaryLog::Version, FramePerformanceCounters::Version);
}

struct FileHeader {
  LARGE_INTEGER mQueryPerformanceFrequency {};
  LARGE_INTEGER mQueryPerformanceCounter {};
  uint64_t mMicrosecondsSinceEpoch {};

  static FileHeader Now() {
    FileHeader ret {};
    QueryPerformanceFrequency(&ret.mQueryPerformanceFrequency);
    QueryPerformanceCounter(&ret.mQueryPerformanceCounter);

    static_assert(
      __cpp_lib_chrono >= 201907L,
      "Need std::chrono::system_clock to be guaranteed to use the Unix epoch");
    ret.mMicrosecondsSinceEpoch
      = duration_cast<std::chrono::microseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count();
    return ret;
  }

  static FileHeader FromData(const void* data, const std::size_t size) {
    if (size != sizeof(FileHeader)) [[unlikely]] {
      throw std::logic_error(
        "Calling FileHeader::FromData with incorrect size");
    }
    FileHeader ret {};
    memcpy(&ret, data, size);
    return ret;
  }

 private:
  FileHeader() = default;
};
// Assert it's the same size in all builds, especially 32- vs 64-bit
static_assert(sizeof(FileHeader) == 24);

struct PacketHeader {
  enum class PacketType : uint32_t {
    Invalid = 0,
    Core,// First packet of each frame
    GpuTime,
    VRAM,
    NVAPI,
    NVEncSession,
  };
  PacketType mType {};
  uint32_t mSize {};

  explicit operator std::string_view() const {
    return std::string_view {
      reinterpret_cast<const char*>(this),
      sizeof(*this),
    };
  }
};
static_assert(sizeof(PacketHeader) == 8);

};// namespace BinaryLog