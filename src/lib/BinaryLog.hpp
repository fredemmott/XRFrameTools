// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT
#pragma once

#include <format>

#include "FramePerformanceCounters.hpp"

namespace BinaryLog {
/* Binary Log Format v2024-12-10#01
 * ================================
 *
 * The binary log contains:
 * 1. a human-readable header
 * 2. a `LARGE_INTEGER` (int64_t) containing the result of
 *   `QueryPerformanceFrequency()`
 * 3. a contiguous stream of `FramePerformanceCounter` structs
 *
 * There is no separator between sections or between
 * `FramePerformanceCounter` structs.
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
 * VERSION_LINE\n
 * FULL_PATH_TO_EXECUTABLE\n
 * ```
 */
static constexpr auto Version = "2024-12-10#01";
static constexpr auto Magic = "XRFrameTools binary log";

inline auto GetVersionLine() noexcept {
  return std::format(
    "BLv{}/FPCv{}", BinaryLog::Version, FramePerformanceCounters::Version);
}
};// namespace BinaryLog