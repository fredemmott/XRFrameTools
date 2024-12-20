// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT

#include "CSVWriter.hpp"

#include <Windows.h>
#include <nvapi.h>
#include <wil/filesystem.h>

#include <ranges>

#include "MetricsAggregator.hpp"
#include "Win32Utils.hpp"

using namespace std::string_literals;

namespace {
auto ECFromWin32(DWORD value) {
  return std::error_code {HRESULT_FROM_WIN32(value), std::system_category()};
}

template <detail::CompileTimeStringHelper V>
consteval auto operator"" _cl() {
  return V;
}

enum class ColumnUnit {
  Counter,
  Micros,
  Bytes,
  Opaque,
  Boolean,
};

template <auto TName, ColumnUnit TUnit, auto TGetter>
struct Column {
  static constexpr auto Name = TName;
  static constexpr auto Unit = TUnit;
  static constexpr auto Getter = TGetter;

  static std::string GetHeader() {
    if constexpr (TUnit == ColumnUnit::Micros) {
      return std::format("{} (µs)", std::string_view {TName});
    } else {
      return std::string {std::string_view {TName}};
    }
  }

  static auto GetValue(const FrameMetrics& afm) {
    return ConvertValue(GetRawValue(afm));
  }

 private:
  static auto GetRawValue(const FrameMetrics& afm) {
    if constexpr (std::invocable<decltype(TGetter), FrameMetrics>) {
      return std::invoke(TGetter, afm);
    } else if constexpr (std::invocable<
                           decltype(TGetter),
                           DXGI_QUERY_VIDEO_MEMORY_INFO>) {
      return std::invoke(TGetter, afm.mVideoMemoryInfo);
    }
  }

  template <class T>
  static constexpr auto ConvertValue(T value) {
    if constexpr (std::same_as<std::decay_t<T>, std::chrono::microseconds>) {
      return std::format("{}", value.count());
    } else {
      return std::format("{}", value);
    }
  }
};

template <FramePerformanceCounters::ValidDataBits TBit>
bool HasData(const FrameMetrics& afm) {
  return (afm.mValidDataBits & std::to_underlying(TBit))
    == std::to_underlying(TBit);
}
constexpr auto& HasNVAPI
  = HasData<FramePerformanceCounters::ValidDataBits::NVAPI>;

template <uint32_t TNVidiaBits>
bool HasAnyOfGPUPerfDecreaseBits(const FrameMetrics& frame) {
  if (HasNVAPI(frame)) {
    return (frame.mGpuPerformanceDecreaseReasons & TNVidiaBits) != 0;
  }
  return false;
}

using Row = std::tuple<
  Column<"Count"_cl, ColumnUnit::Counter, &FrameMetrics::mFrameCount>,
  Column<"App CPU"_cl, ColumnUnit::Micros, &FrameMetrics::mAppCpu>,
  Column<"Wait CPU"_cl, ColumnUnit::Micros, &FrameMetrics::mWaitFrameCpu>,
  Column<"Begin CPU"_cl, ColumnUnit::Micros, &FrameMetrics::mBeginFrameCpu>,
  Column<"Render CPU"_cl, ColumnUnit::Micros, &FrameMetrics::mRenderCpu>,
  Column<"Submit CPU"_cl, ColumnUnit::Micros, &FrameMetrics::mEndFrameCpu>,
  Column<"Render GPU"_cl, ColumnUnit::Micros, &FrameMetrics::mRenderGpu>,
  Column<
    "Frame Interval"_cl,
    ColumnUnit::Micros,
    &FrameMetrics::mSincePreviousFrame>,
  Column<
    "FPS"_cl,
    ColumnUnit::Counter,
    [](const FrameMetrics& frame) {
      return 1.0e6 / frame.mSincePreviousFrame.count();
    }>,
  Column<
    "VRAM Budget"_cl,
    ColumnUnit::Bytes,
    &DXGI_QUERY_VIDEO_MEMORY_INFO::Budget>,
  Column<
    "VRAM Current Usage "_cl,
    ColumnUnit::Bytes,
    &DXGI_QUERY_VIDEO_MEMORY_INFO::CurrentUsage>,
  Column<
    "VRAM Current Reserveration"_cl,
    ColumnUnit::Bytes,
    &DXGI_QUERY_VIDEO_MEMORY_INFO::CurrentReservation>,
  Column<
    "VRAM Available for Reserveration"_cl,
    ColumnUnit::Bytes,
    &DXGI_QUERY_VIDEO_MEMORY_INFO::AvailableForReservation>,
  Column<
    "GPU API"_cl,
    ColumnUnit::Opaque,
    [](const FrameMetrics& frame) { return HasNVAPI(frame) ? "NVAPI" : ""; }>,
  Column<
    "GPU P-State Min"_cl,
    ColumnUnit::Opaque,
    &FrameMetrics::mGpuLowestPState>,
  Column<
    "GPU P-State Max"_cl,
    ColumnUnit::Opaque,
    &FrameMetrics::mGpuHighestPState>,
  Column<
    "GPU Limit Bits"_cl,
    ColumnUnit::Opaque,
    [](const FrameMetrics& frame) {
      return frame.mGpuPerformanceDecreaseReasons;
    }>,
  Column<
    "GPU Thermal Limit"_cl,
    ColumnUnit::Boolean,
    &HasAnyOfGPUPerfDecreaseBits<
      NV_GPU_PERF_DECREASE_REASON_THERMAL_PROTECTION>>,
  Column<
    "GPU Power Limit"_cl,
    ColumnUnit::Boolean,
    &HasAnyOfGPUPerfDecreaseBits<
      NV_GPU_PERF_DECREASE_REASON_POWER_CONTROL
      | NV_GPU_PERF_DECREASE_REASON_AC_BATT
      | NV_GPU_PERF_DECREASE_REASON_INSUFFICIENT_POWER>>,
  Column<
    "GPU API Limit"_cl,
    ColumnUnit::Boolean,
    &HasAnyOfGPUPerfDecreaseBits<NV_GPU_PERF_DECREASE_REASON_API_TRIGGERED>>>;

std::string GetColumnHeaders() {
  return []<std::size_t... I>(std::index_sequence<I...>) {
    return std::array {std::tuple_element_t<I, Row>::GetHeader()...}
    | std::views::join_with(","s) | std::ranges::to<std::string>();
  }(std::make_index_sequence<std::tuple_size_v<Row>> {});
}

std::string GetRow(const FrameMetrics& frame) {
  return [&frame]<std::size_t... I>(std::index_sequence<I...>) {
    return std::array {std::tuple_element_t<I, Row>::GetValue(frame)...}
    | std::views::join_with(","s) | std::ranges::to<std::string>();
  }(std::make_index_sequence<std::tuple_size_v<Row>> {});
}

}// namespace

CSVWriter::Result CSVWriter::Write(
  BinaryLogReader reader,
  const std::filesystem::path& outputPath,
  size_t framesPerRow) {
  if (!std::filesystem::exists(outputPath.parent_path())) {
    std::filesystem::create_directories(outputPath.parent_path());
  }

  auto [handle, error] = wil::try_open_or_truncate_existing_file(
    outputPath.wstring().c_str(), GENERIC_WRITE);
  if (!handle) {
    throw std::filesystem::filesystem_error {
      "Couldn't open output file",
      outputPath,
      ECFromWin32(error),
    };
  }

  return Write(std::move(reader), handle.get(), framesPerRow);
}

CSVWriter::Result
CSVWriter::Write(BinaryLogReader reader, HANDLE out, size_t framesPerRow) {
  const auto pcm = reader.GetPerformanceCounterMath();
  Result ret;

  // Include the UTF-8 Byte Order Mark as Excel and Google Sheets use it
  // as a magic value for UTF-8
  win32::println(
    out, "\ufeffTime (µs),Time (UTC),Time (Local),{}", GetColumnHeaders());

  auto& frameCount = ret.mFrameCount;
  auto& flushCount = ret.mRowCount;
  MetricsAggregator acc {pcm};
  std::optional<LARGE_INTEGER> firstFrameTime {};
  LARGE_INTEGER lastFrameTime {};

  const auto ToUTC = [clockCalibration = reader.GetClockCalibration(),
                      pcm](const LARGE_INTEGER& time) {
    const auto sinceCalibration
      = pcm.ToDuration(clockCalibration.mQueryPerformanceCounter, time);
    const auto sinceEpoch = sinceCalibration
      + std::chrono::microseconds(clockCalibration.mMicrosecondsSinceEpoch);
    static_assert(
      __cpp_lib_chrono >= 201907L,
      "Need std::chrono::system_clock to be guaranteed to be UTC, using the "
      "Unix epoch");
    return time_point_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::time_point(sinceEpoch));
  };

  const auto tz = std::chrono::current_zone();

  while (const auto frame = reader.GetNextFrame()) {
    if (!firstFrameTime) {
      firstFrameTime = frame->mEndFrameStop;
    }
    lastFrameTime = frame->mEndFrameStop;

    acc.Push(*frame);
    if (++frameCount % framesPerRow != 0) {
      continue;
    }
    const auto row = acc.Flush();
    if (!row) {
      continue;
    };

    const auto utc = ToUTC(frame->mEndFrameStop);
    const auto localTime = std::chrono::zoned_time(tz, utc);

    win32::println(
      out,
      R"({},"{:%FT%T}","{:%FT%T}",{})",
      pcm.ToDuration(*firstFrameTime, frame->mEndFrameStop).count(),
      utc,
      localTime,
      GetRow(*row));
    ++flushCount;
  }

  if (firstFrameTime) {
    ret.mLogDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
      pcm.ToDuration(*firstFrameTime, lastFrameTime));
  }

  return ret;
}
