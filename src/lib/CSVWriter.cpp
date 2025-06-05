// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT

#include "CSVWriter.hpp"

#include <Windows.h>
#include <nvapi.h>
#include <wil/filesystem.h>

#include <functional>
#include <ranges>

#include "MetricsAggregator.hpp"
#include "Win32Utils.hpp"

using namespace std::string_literals;

namespace {
auto ECFromWin32(DWORD value) {
  return std::error_code {HRESULT_FROM_WIN32(value), std::system_category()};
}

enum class ColumnUnit {
  Counter,
  Micros,
  Bytes,
  KHz,
  Opaque,
  Boolean,
};

template <class T, class TRet = std::string, class TParam = FrameMetrics>
concept Getter = std::invocable<T, TParam>
  && std::convertible_to<std::invoke_result_t<T, TParam>, TRet>;
template <class T, class TParam = FrameMetrics>
concept NumericGetter = Getter<T, std::invoke_result_t<T, TParam>, TParam>
  && (std::integral<std::decay_t<std::invoke_result_t<T, TParam>>>
      || std::floating_point<std::decay_t<std::invoke_result_t<T, TParam>>>);

class Column {
 public:
  Column() = delete;
  Column(std::string_view name, ColumnUnit unit, Getter auto getter)
    : mName(name), mUnit(unit), mGetter([getter](const FrameMetrics& fm) {
        return std::invoke(getter, fm);
      }) {
  }

  Column(std::string_view name, ColumnUnit unit, NumericGetter auto getter)
    : Column(name, unit, [getter](const FrameMetrics& fm) {
        return std::to_string(std::invoke(getter, fm));
      }) {
  }

  Column(std::string_view name, Getter<std::chrono::microseconds> auto getter)
    : Column(name, ColumnUnit::Micros, [getter](const FrameMetrics& fm) {
        return std::to_string(std::invoke(getter, fm).count());
      }) {
  }

  Column(
    std::string_view name,
    Getter<uint64_t, DXGI_QUERY_VIDEO_MEMORY_INFO> auto getter)
    : Column(name, ColumnUnit::Bytes, [getter](const FrameMetrics& fm) {
        return std::to_string(std::invoke(getter, fm.mVideoMemoryInfo));
      }) {
  }

  std::string GetHeader() const {
    switch (mUnit) {
      case ColumnUnit::Micros:
        return std::format("{} (µs)", mName);
      case ColumnUnit::KHz:
        return std::format("{} (KHz)", mName);
      default:
        return mName;
    }
  }

  std::string GetValue(const FrameMetrics& afm) const {
    return std::invoke(mGetter, afm);
  }

 private:
  std::string mName;
  ColumnUnit mUnit;
  std::function<std::string(const FrameMetrics&)> mGetter;
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

const auto BaseColumns = std::array {
  Column {
    "Display XrTime",
    ColumnUnit::Opaque,
    &FrameMetrics::mLastXrDisplayTime,
  },
  Column {
    "Frame Interval",
    &FrameMetrics::mSincePreviousFrame,
  },
  Column {
    "FPS",
    ColumnUnit::Counter,
    [](const FrameMetrics& frame) {
      return 1.0e6 / frame.mSincePreviousFrame.count();
    },
  },
  Column {
    "Count",
    ColumnUnit::Counter,
    &FrameMetrics::mFrameCount,
  },
  Column {
    "App CPU",
    &FrameMetrics::mAppCpu,
  },
  Column {
    "Render CPU",
    &FrameMetrics::mRenderCpu,
  },
  Column {
    "Render GPU",
    &FrameMetrics::mRenderGpu,
  },
  Column {
    "Wait CPU",
    &FrameMetrics::mWaitFrameCpu,
  },
  Column {
    "Begin CPU",
    &FrameMetrics::mBeginFrameCpu,
  },
  Column {
    "Submit CPU",
    &FrameMetrics::mEndFrameCpu,
  },
  Column {
    "VRAM Budget",
    &DXGI_QUERY_VIDEO_MEMORY_INFO::Budget,
  },
  Column {
    "VRAM Current Usage ",
    &DXGI_QUERY_VIDEO_MEMORY_INFO::CurrentUsage,
  },
  Column {
    "VRAM Current Reserveration",
    &DXGI_QUERY_VIDEO_MEMORY_INFO::CurrentReservation,
  },
  Column {
    "VRAM Available for Reserveration",
    &DXGI_QUERY_VIDEO_MEMORY_INFO::AvailableForReservation,
  },
  Column {
    "GPU API",
    ColumnUnit::Opaque,
    [](const FrameMetrics& frame) { return HasNVAPI(frame) ? "NVAPI" : ""; },
  },
  Column {
    "GPU Clock Min",
    ColumnUnit::KHz,
    &FrameMetrics::mGpuGraphicsKHzMin,
  },
  Column {
    "GPU Clock Max",
    ColumnUnit::KHz,
    &FrameMetrics::mGpuGraphicsKHzMax,
  },
  Column {
    "GPU VRAM Clock Min",
    ColumnUnit::KHz,
    &FrameMetrics::mGpuMemoryKHzMin,
  },
  Column {
    "GPU VRAM Clock Max",
    ColumnUnit::KHz,
    &FrameMetrics::mGpuMemoryKHzMax,
  },
  Column {
    "GPU P-State Min",
    ColumnUnit::Opaque,
    &FrameMetrics::mGpuPStateMin,
  },
  Column {
    "GPU P-State Max",
    ColumnUnit::Opaque,
    &FrameMetrics::mGpuPStateMax,
  },
  Column {
    "GPU Limit Bits",
    ColumnUnit::Opaque,
    [](const FrameMetrics& frame) {
      return frame.mGpuPerformanceDecreaseReasons;
    },
  },
  Column {
    "GPU Thermal Limit",
    ColumnUnit::Boolean,
    &HasAnyOfGPUPerfDecreaseBits<
      NV_GPU_PERF_DECREASE_REASON_THERMAL_PROTECTION>,
  },
  Column {
    "GPU Power Limit",
    ColumnUnit::Boolean,
    &HasAnyOfGPUPerfDecreaseBits<
      NV_GPU_PERF_DECREASE_REASON_POWER_CONTROL
      | NV_GPU_PERF_DECREASE_REASON_AC_BATT
      | NV_GPU_PERF_DECREASE_REASON_INSUFFICIENT_POWER>,
  },
  Column {
    "GPU API Limit",
    ColumnUnit::Boolean,
    &HasAnyOfGPUPerfDecreaseBits<NV_GPU_PERF_DECREASE_REASON_API_TRIGGERED>,
  },
};

std::string GetColumnHeaders(const auto& columns) {
  return columns | std::views::transform(&Column::GetHeader)
    | std::views::join_with(","s) | std::ranges::to<std::string>();
}

std::string GetRow(const auto& columns, const FrameMetrics& frame) {
  return columns | std::views::transform([&frame](const Column& column) {
           return column.GetValue(frame);
         })
    | std::views::join_with(","s) | std::ranges::to<std::string>();
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

  auto columns = std::vector<Column> {BaseColumns.begin(), BaseColumns.end()};

  // Include the UTF-8 Byte Order Mark, because Excel and Google Sheets use it
  // as a magic value for UTF-8
  win32::println(
    out,
    "\ufeffTime (µs),Time (UTC),Time (Local),{}",
    GetColumnHeaders(columns));

  auto& frameCount = ret.mFrameCount;
  auto& flushCount = ret.mRowCount;
  MetricsAggregator acc {pcm};
  std::optional<LARGE_INTEGER> firstFrameTime {};
  LARGE_INTEGER lastFrameTime {};

  const auto ToUTC = [clockCalibration = reader.GetClockCalibration(),
                      pcm](const LARGE_INTEGER& time) {
    // As the binary logging happens in its' own thread, it's possible for
    // the first few threads to have an end time that is earlier than the
    // log start time
    const auto sinceCalibration = pcm.ToDurationAllowNegative(
      clockCalibration.mQueryPerformanceCounter, time);
    const auto sinceEpoch = sinceCalibration
      + std::chrono::microseconds(clockCalibration.mMicrosecondsSinceEpoch);
    static_assert(
      __cpp_lib_chrono >= 201907L,
      "Need std::chrono::system_clock to be guaranteed to be UTC, using "
      "the "
      "Unix epoch");
    return time_point_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::time_point(sinceEpoch));
  };

  const auto tz = std::chrono::current_zone();

  while (const auto frame = reader.GetNextFrame()) {
    const auto& core = frame->mCore;
    if (!firstFrameTime) {
      firstFrameTime = core.mEndFrameStop;
    }
    lastFrameTime = core.mEndFrameStop;

    acc.Push(*frame);
    if (++frameCount % framesPerRow != 0) {
      continue;
    }
    const auto row = acc.Flush();
    if (!row) {
      continue;
    };

    const auto utc = ToUTC(core.mEndFrameStop);
    const auto localTime = std::chrono::zoned_time(tz, utc);

    win32::println(
      out,
      R"({},"{:%FT%T}","{:%FT%T}",{})",
      pcm.ToDuration(*firstFrameTime, core.mEndFrameStop).count(),
      utc,
      localTime,
      GetRow(columns, *row));
    ++flushCount;
  }

  if (firstFrameTime) {
    ret.mLogDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
      pcm.ToDuration(*firstFrameTime, lastFrameTime));
  }

  return ret;
}
