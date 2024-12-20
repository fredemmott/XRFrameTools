// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT
#pragma once

#include <imgui.h>
#include <implot.h>

#include <vector>

#include "AutoUpdater.hpp"
#include "BinaryLogReader.hpp"
#include "CSVWriter.hpp"
#include "Config.hpp"
#include "ContiguousRingBuffer.hpp"
#include "ImStackedAreaPlotter.hpp"
#include "MetricsAggregator.hpp"
#include "SHMReader.hpp"
#include "Window.hpp"

struct LiveData {
  LiveData();
  template <class T>
  struct PlotPoint : ImPlotPoint {
    PlotPoint(const int idx, const T value)
      : ImPlotPoint(
          static_cast<double>(idx) / LiveData::ChartFPS,
          static_cast<double>(value)) {
    }
  };

  static constexpr size_t ChartFPS = 30;
  static constexpr auto ChartInterval
    = std::chrono::microseconds(1000000) / ChartFPS;

  static constexpr size_t HistorySeconds = 30;
  static constexpr size_t BufferSize = ChartFPS * HistorySeconds;

  wil::unique_handle mInterruptEvent {
    CreateEventW(nullptr, FALSE, FALSE, nullptr)};

  bool mEnabled {true};
  std::chrono::steady_clock::time_point mLastChartFrameAt {};
  LARGE_INTEGER mLatestMetricsAt {};
  FrameMetrics mLatestMetrics {};

  uint64_t mSHMFrameIndex {};

  MetricsAggregator mAggregator;

  using ChartFrames = ContiguousRingBuffer<FrameMetrics, BufferSize>;

  ChartFrames mChartFrames {BufferSize};

  template <auto Getter>
  static ImPlotPoint PlotFrame(int idx, void* user_data) {
    const auto& frame = static_cast<LiveData::ChartFrames*>(user_data)->at(idx);
    return PlotPoint {
      idx,
      std::invoke(Getter, frame),
    };
  }

  template <auto Getter>
  static ImPlotPoint PlotMicroseconds(int idx, void* user_data) {
    return PlotFrame<[](const FrameMetrics& frame) {
      return duration_cast<std::chrono::microseconds>(
               std::invoke(Getter, frame))
        .count();
    }>(idx, user_data);
  }

  template <auto Getter>
  static ImPlotPoint PlotVideoMemory(int idx, void* user_data) {
    return PlotFrame<[](const FrameMetrics& frame) {
      return std::invoke(Getter, frame.mVideoMemoryInfo) / (1024 * 1024);
    }>(idx, user_data);
  }
};

class MainWindow final : public Window {
 public:
  explicit MainWindow(const HINSTANCE instance);
  ~MainWindow() override;

 protected:
  using Window::Window;

  void RenderContent() override;
  [[nodiscard]] std::optional<float> GetTargetFPS() const noexcept override;

 private:
  AutoUpdater mUpdater;
  Config mBaseConfig;
  std::filesystem::path mThisExecutable;

  int mCSVFramesPerRow {CSVWriter::DefaultFramesPerRow};
  std::vector<BinaryLogReader> mBinaryLogFiles;
  [[nodiscard]] std::vector<BinaryLogReader> PickBinaryLogFiles();
  void ConvertBinaryLogFiles();
  void LoggingControls();
  void LogConversionControls();
  void LoggingSection();
  void PlotNVAPI();
  void PlotFramerate(double maxMicroseconds);
  void PlotFrameTimings(double maxMicroseconds);
  void PlotVideoMemory();
  void LiveDataSection();
  void AboutSection();

  void UpdateLiveData();

  SHMReader mSHM;
  ImStackedAreaPlotter::Kind mFrameTimingPlotKind {
    ImStackedAreaPlotter::Kind::StackedArea};

  struct LiveApp {
    DWORD mProcessID {};
    std::optional<uint8_t> mProcessBitness;
    std::filesystem::path mExecutablePath;
  };
  LiveApp mLiveApp;

  LiveData mLiveData;
  std::mutex mLiveDataMutex;
  std::jthread mLiveDataThread;
  void UpdateLiveDataThreadEntry(const std::stop_token);
};