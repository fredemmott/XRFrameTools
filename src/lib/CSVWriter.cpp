// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT

#include "CSVWriter.hpp"

#include <Windows.h>
#include <nvapi.h>
#include <wil/filesystem.h>

#include "MetricsAggregator.hpp"
#include "Win32Utils.hpp"

static auto ECFromWin32(DWORD value) {
  return std::error_code {HRESULT_FROM_WIN32(value), std::system_category()};
}

static auto ECFromHRESULT(HRESULT value) {
  return std::error_code {value, std::system_category()};
}

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
    out,
    "\ufeffTime (µs),Count,Wait CPU (µs),App CPU (µs),Runtime CPU (µs),Render "
    "CPU (µs),Render GPU (µs),Interval (µs),FPS,VRAM Budget,VRAM Current "
    "Usage,VRAM Current Reservation,VRAM Available for Reservation,GPU "
    "P-State Min,GPU P-State Max,GPU API,GPU Limit Bits,GPU Thermal "
    "Limit,GPU Power Limit,GPU API Limit");

  auto& frameCount = ret.mFrameCount;
  auto& flushCount = ret.mRowCount;
  MetricsAggregator acc {pcm};
  std::optional<LARGE_INTEGER> firstFrameTime {};
  LARGE_INTEGER lastFrameTime {};

  while (const auto frame = reader.GetNextFrame()) {
    if (!firstFrameTime) {
      firstFrameTime = frame->mEndFrameStart;
    }
    lastFrameTime = frame->mEndFrameStart;

    acc.Push(*frame);
    if (++frameCount % framesPerRow != 0) {
      continue;
    }
    const auto row = acc.Flush();
    if (!row) {
      continue;
    };

    const bool haveNVAPI = row->mValidDataBits
      & std::to_underlying(FramePerformanceCounters::ValidDataBits::NVAPI);
    const auto gpuDataSource = haveNVAPI ? "NVAPI" : "";

    const auto gpuPowerLimitFlags = [haveNVAPI, row](auto flags) {
      if (!haveNVAPI) {
        return "";
      }
      return ((row->mGpuPerformanceInfo.mDecreaseReason & flags) != 0) ? "1"
                                                                       : "0";
    };

    win32::println(
      out,
      "{},{},{},{},{},{},{},{},{:0.1f},{},{},{},{},{},{},{},{},{},{},{}",
      pcm.ToDuration(*firstFrameTime, frame->mEndFrameStart).count(),
      row->mFrameCount,
      row->mWaitCpu.count(),
      row->mAppCpu.count(),
      row->mRuntimeCpu.count(),
      row->mRenderCpu.count(),
      row->mRenderGpu.count(),
      row->mSincePreviousFrame.count(),
      1000000.0f / row->mSincePreviousFrame.count(),
      row->mVideoMemoryInfo.Budget,
      row->mVideoMemoryInfo.CurrentUsage,
      row->mVideoMemoryInfo.CurrentReservation,
      row->mVideoMemoryInfo.AvailableForReservation,
      row->mGpuLowestPState,
      row->mGpuHighestPState,
      gpuDataSource,
      row->mGpuPerformanceInfo.mDecreaseReason,
      gpuPowerLimitFlags(NV_GPU_PERF_DECREASE_REASON_THERMAL_PROTECTION),
      gpuPowerLimitFlags(
        NV_GPU_PERF_DECREASE_REASON_POWER_CONTROL
        | NV_GPU_PERF_DECREASE_REASON_AC_BATT
        | NV_GPU_PERF_DECREASE_REASON_INSUFFICIENT_POWER),
      gpuPowerLimitFlags(NV_GPU_PERF_DECREASE_REASON_API_TRIGGERED));
    ++flushCount;
  }

  if (firstFrameTime) {
    ret.mLogDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
      pcm.ToDuration(*firstFrameTime, lastFrameTime));
  }

  return ret;
}
