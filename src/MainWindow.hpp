// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT
#pragma once

#include <vector>

#include "BinaryLogReader.hpp"
#include "CSVWriter.hpp"
#include "Config.hpp"
#include "ContiguousRingBuffer.hpp"
#include "ImStackedAreaPlotter.hpp"
#include "MetricsAggregator.hpp"
#include "SHMReader.hpp"
#include "Window.hpp"

class MainWindow final : public Window {
 public:
  explicit MainWindow(const HINSTANCE instance);
  ~MainWindow() override;

 protected:
  using Window::Window;

  void RenderContent() override;
  [[nodiscard]] std::optional<float> GetTargetFPS() const noexcept override;

 private:
  Config mBaseConfig;
  std::filesystem::path mThisExecutable;

  int mCSVFramesPerRow {CSVWriter::DefaultFramesPerRow};
  std::vector<BinaryLogReader> mBinaryLogFiles;
  [[nodiscard]] std::vector<BinaryLogReader> PickBinaryLogFiles();
  void ConvertBinaryLogFiles();
  void LoggingControls();
  void LogConversionControls();
  void LoggingSection();
  void LiveDataSection();
  void AboutSection();

  void UpdateLiveData();

  SHMReader mSHM;
  ImStackedAreaPlotter::Kind mFrameTimingPlotKind {
    ImStackedAreaPlotter::Kind::StackedArea};

  struct LiveApp {
    DWORD mProcessID {};
    std::filesystem::path mExecutablePath;
  };
  LiveApp mLiveApp;

  struct LiveData {
    LiveData();
    template <class T>
    struct PlotPoint;

    static constexpr size_t ChartFPS = 30;
    static constexpr auto ChartInterval
      = std::chrono::microseconds(1000000) / ChartFPS;

    static constexpr size_t HistorySeconds = 30;
    static constexpr size_t BufferSize = ChartFPS * HistorySeconds;

    bool mEnabled {true};
    std::chrono::steady_clock::time_point mLastChartFrameAt {};
    LARGE_INTEGER mLatestMetricsAt {};
    AggregatedFrameMetrics mLatestMetrics {};

    uint64_t mSHMFrameIndex {};

    MetricsAggregator mAggregator;

    using ChartFrames
      = ContiguousRingBuffer<AggregatedFrameMetrics, BufferSize>;

    ChartFrames mChartFrames {BufferSize};
  } mLiveData;
  std::mutex mLiveDataMutex;
  std::jthread mLiveDataThread;
  void UpdateLiveDataThreadEntry(const std::stop_token);
};