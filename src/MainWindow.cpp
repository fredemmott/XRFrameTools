// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT

#include "MainWindow.hpp"

#include <implot.h>
#include <shellapi.h>
#include <shlobj_core.h>
#include <wil/com.h>
#include <wil/resource.h>
#include <wil/win32_helpers.h>

#include <magic_enum.hpp>
#include <ranges>

#include "CSVWriter.hpp"
#include "CheckHResult.hpp"
#include "ImGuiHelpers.hpp"
#include "SHM.hpp"
#include "Win32Utils.hpp"

static const auto gPCM = PerformanceCounterMath::CreateForLiveData();

MainWindow::MainWindow(HINSTANCE instance)
  : Window(instance, L"XRFrameTools"),
    mBaseConfig(Config::GetUserDefaults(Config::Access::ReadWrite)) {
}

MainWindow::~MainWindow() {
}

MainWindow::LiveData::LiveData() : mAggregator(gPCM) {
}

template <
  class Container,
  class Element = std::ranges::range_value_t<Container>,
  std::regular_invocable<Element> Projection,
  class Projected = std::invoke_result_t<Projection, const Element&>>
static Projected SingleValue(
  Container&& container,
  std::type_identity_t<Projected>&& init,
  std::type_identity_t<Projected>&& varied,
  Projection proj) {
  std::optional<Projected> acc {std::nullopt};
  for (auto&& it: std::forward<Container>(container)) {
    const auto value = std::invoke(proj, it);
    if (!acc.has_value()) {
      acc = value;
      continue;
    }
    if (acc != value) {
      return std::forward<decltype(varied)>(varied);
    }
  }
  return acc.value_or(std::forward<decltype(init)>(init));
}

void MainWindow::LoggingControls() {
  auto& config = mBaseConfig;

  std::string loggingState;
  switch (const auto value = config.GetBinaryLoggingEnabledUntil()) {
    case Config::BinaryLoggingDisabled:
      loggingState = "disabled";
      break;
    case Config::BinaryLoggingPermanentlyEnabled:
      loggingState = "enabled";
      break;
    default: {
      const auto timestamp
        = std::chrono::system_clock::time_point {std::chrono::seconds {value}};
      const auto zoned = std::chrono::zoned_time(
        std::chrono::current_zone(),
        std::chrono::time_point_cast<std::chrono::seconds>(timestamp));
      const auto now = std::chrono::system_clock::now();
      if (timestamp > now) {
        loggingState = std::format("enabled until {}", zoned);
      } else {
        loggingState = std::format("finished at {}", zoned);
      }
    }
  }
  ImGui::Text("%s", loggingState.c_str());
  ImGui::SameLine();
  {
    const ImGuiScoped::EnabledIf enabled(config.IsBinaryLoggingEnabled());
    if (ImGui::Button("Disable")) {
      config.SetBinaryLoggingEnabledUntil(Config::BinaryLoggingDisabled);
    }
  }
  ImGui::SameLine();
  {
    const ImGuiScoped::DisabledIf disabled(
      config.GetBinaryLoggingEnabledUntil()
      == Config::BinaryLoggingPermanentlyEnabled);
    if (ImGui::Button("Enable")) {
      config.SetBinaryLoggingEnabledUntil(
        Config::BinaryLoggingPermanentlyEnabled);
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("\ue916Enable for...")) {
    ImGui::OpenPopup("EnablePopup");
  }
  if (const auto popup = ImGuiScoped::Popup("EnablePopup")) {
    constexpr auto names = std::array {
      "10 seconds",
      "1 minute",
      "5 minutes",
      "15 minutes",
      "1 hour",
      "6 hours",
      "24 hours",
    };
    constexpr std::chrono::seconds values[] {
      std::chrono::seconds {10},
      std::chrono::minutes {1},
      std::chrono::minutes {5},
      std::chrono::minutes {15},
      std::chrono::hours {1},
      std::chrono::hours {6},
      std::chrono::hours {24}};
    static_assert(std::size(names) == std::size(values));

    for (size_t i = 0; i < std::size(names); i++) {
      if (ImGui::Selectable(names[i])) {
        const auto endAt = std::chrono::system_clock::now() + values[i];
        const auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                                 endAt.time_since_epoch())
                                 .count();
        config.SetBinaryLoggingEnabledUntil(timestamp);
      }
    }
  }
}

void MainWindow::LogConversionControls() {
  // "ReportDocument" glyph
  if (ImGui::Button("\ue9f9 Convert log files to CSV...")) {
    ImGui::OpenPopup("Convert log files to CSV");
  }

  const auto popup = ImGuiScoped::PopupModal(
    "Convert log files to CSV", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
  if (!popup) {
    return;
  }

  if (mBinaryLogFiles.empty()) {
    auto files = PickBinaryLogFiles();
    if (files.empty()) {
      ImGui::CloseCurrentPopup();
      return;
    }
    mBinaryLogFiles = std::move(files);
  }

  const auto resolution = SingleValue(
    mBinaryLogFiles, "no log files", "varied", [](const auto& log) {
      return std::format(
        "{}hz", log.GetPerformanceCounterMath().GetResolution().QuadPart);
    });
  ImGui::LabelText("Resolution", "%s", resolution.c_str());

  const auto executable = SingleValue(
    mBinaryLogFiles, "no log files", "varied", [](const auto& log) {
      return log.GetExecutablePath().string();
    });
  ImGui::LabelText("Application path", "%s", executable.c_str());

  int aggregateFrames = 10;
  if (ImGui::InputInt("Frames per CSV row (averaged)", &mCSVFramesPerRow)) {
    if (mCSVFramesPerRow < 1) {
      mCSVFramesPerRow = 1;
    }
  }

  // "SaveAs" Glyph
  const auto saveAsLabel = std::format(
    "\ue792 {}...", mBinaryLogFiles.size() == 1 ? "Save as" : "Save to folder");
  if (ImGui::Button(saveAsLabel.c_str())) {
    this->ConvertBinaryLogFiles();
    ImGui::CloseCurrentPopup();
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel")) {
    mBinaryLogFiles.clear();
    ImGui::CloseCurrentPopup();
  }
}
void MainWindow::LoggingSection() {
  const ImGuiScoped::ID idScope {"Logging"};

  // "History" glyph
  ImGui::SeparatorText("\ue81c Performance logging");

  this->LoggingControls();

  ImGui::Separator();

  this->LogConversionControls();

  // "OpenFolderHorizontal"
  if (ImGui::Button("\ued25 Open logs folder...")) {
    ShellExecuteW(
      GetHWND(),
      L"explore",
      (GetKnownFolderPath(FOLDERID_LocalAppData) / "XRFrameTools" / "Logs")
        .wstring()
        .c_str(),
      nullptr,
      nullptr,
      SW_SHOWNORMAL);
  }
}

void MainWindow::RenderContent() {
  // Unicode escapes are glyphs from the Windows icon fonts:
  // - "Segoe MDL2 Assets" on Win10+ (including Win11)
  // - "Segoe Fluent Icons" on Win11+
  //
  // We use Fluent if available, but fallback to MDL2, so pick glyphs that are
  // in both fonts (Fluent includes all from MDL2 Assets)
  //
  // Reference:
  //
  // https://learn.microsoft.com/en-us/windows/apps/design/style/segoe-ui-symbol-font

  this->LoggingSection();
  this->LiveDataSection();
}
std::optional<float> MainWindow::GetTargetFPS() const noexcept {
  if (!mLiveData.mEnabled) {
    return std::nullopt;
  }

  LARGE_INTEGER now {};
  QueryPerformanceCounter(&now);

  const auto liveDataAge = gPCM.ToDuration(mLiveData.mLatestMetricsAt, now);
  if (liveDataAge < std::chrono::seconds(LiveData::HistorySeconds)) {
    return LiveData::ChartFPS;
  }

  // If we have no data, but checking is enabled, let's always wake up at
  // least once per second to find out if we have any
  return 1;
}

std::vector<BinaryLogReader> MainWindow::PickBinaryLogFiles() {
  // used for remembering location/preferences
  constexpr GUID BinaryLogsFilePicker
    = "{f09453d5-0bb2-4c09-971d-b8c4fa45c2c3}"_guid;

  const auto picker
    = wil::CoCreateInstance<IFileOpenDialog>(CLSID_FileOpenDialog);
  CheckHResult(picker->SetClientGuid(BinaryLogsFilePicker));
  picker->SetTitle(L"Open log files");
  picker->SetOptions(
    FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST | FOS_FORCEFILESYSTEM
    | FOS_ALLOWMULTISELECT);

  const auto defaultFolder
    = GetKnownFolderPath(FOLDERID_LocalAppData) / "XRFrameTools" / "Logs";
  if (!std::filesystem::exists(defaultFolder)) {
    std::filesystem::create_directories(defaultFolder);
  }
  wil::com_ptr<IShellItem> defaultFolderShellItem;
  CheckHResult(SHCreateItemFromParsingName(
    defaultFolder.wstring().c_str(),
    nullptr,
    IID_PPV_ARGS(defaultFolderShellItem.put())));
  picker->SetDefaultFolder(defaultFolderShellItem.get());

  constexpr COMDLG_FILTERSPEC fileTypes[] = {
    {L"Logs files", L"*.XRFrameToolsBinLog"},
  };
  picker->SetFileTypes(std::size(fileTypes), fileTypes);
  if (picker->Show(this->GetHWND()) != S_OK) {
    return {};
  }

  wil::com_ptr<IShellItemArray> items;
  CheckHResult(picker->GetResults(items.put()));

  DWORD count {};
  CheckHResult(items->GetCount(&count));
  if (count == 0) {
    return {};
  }

  std::vector<BinaryLogReader> readers;
  for (DWORD i = 0; i < count; i++) {
    wil::com_ptr<IShellItem> it;
    CheckHResult(items->GetItemAt(i, it.put()));
    wil::unique_cotaskmem_string pathString;
    CheckHResult(it->GetDisplayName(SIGDN_FILESYSPATH, pathString.put()));

    const auto path = std::filesystem::canonical({pathString.get()});
    auto reader = BinaryLogReader::Create(path);

    if (reader) {
      readers.push_back(std::move(*reader));
      continue;
    }

    const auto ret = MessageBoxA(
      GetHWND(),
      "Error opening log file",
      std::format(
        "Couldn't open `{}`:\n\n{}",
        path.string(),
        magic_enum::enum_name(reader.error().GetCode()))
        .c_str(),
      MB_ICONEXCLAMATION | MB_OKCANCEL);
    if (ret == IDCANCEL) {
      return {};
    }
  }

  return readers;
}

void MainWindow::ConvertBinaryLogFiles() {
  // used for remembering location/preferences
  constexpr GUID CSVFilePicker = "{31143ff6-b497-406f-a240-f250e3e3c455}"_guid;
  constexpr COMDLG_FILTERSPEC fileTypes[] = {
    {L"CSV Files", L"*.csv"},
  };

  if (mBinaryLogFiles.empty()) {
    return;
  }

  const auto picker = wil::CoCreateInstance<IFileDialog>(
    mBinaryLogFiles.size() == 1 ? CLSID_FileSaveDialog : CLSID_FileOpenDialog);
  picker->SetClientGuid(CSVFilePicker);

  FILEOPENDIALOGOPTIONS options
    = FOS_PATHMUSTEXIST | FOS_FORCEFILESYSTEM | FOS_NOREADONLYRETURN;
  if (mBinaryLogFiles.size() == 1) {
    picker->SetTitle(L"Save CSV file");
    picker->SetFileTypes(std::size(fileTypes), fileTypes);
    picker->SetFileName(
      (mBinaryLogFiles.front().GetLogFilePath().stem().wstring() + L".csv")
        .c_str());
  } else {
    picker->SetTitle(L"Save CSV files");
    picker->SetOkButtonLabel(L"Save to folder");
    options |= FOS_PICKFOLDERS;
  }
  picker->SetOptions(options);

  wil::com_ptr<IShellItem> defaultFolderShellItem;
  CheckHResult(SHCreateItemInKnownFolder(
    FOLDERID_Documents,
    KF_FLAG_DEFAULT,
    nullptr,
    IID_PPV_ARGS(defaultFolderShellItem.put())));
  picker->SetDefaultFolder(defaultFolderShellItem.get());

  if (picker->Show(this->GetHWND()) != S_OK) {
    return;
  }

  wil::com_ptr<IShellItem> outputShellItem;
  CheckHResult(picker->GetResult(outputShellItem.put()));
  wil::unique_cotaskmem_string pathString;
  CheckHResult(
    outputShellItem->GetDisplayName(SIGDN_FILESYSPATH, pathString.put()));
  const auto outputPath = std::filesystem::weakly_canonical({pathString.get()});
  const auto clearOnExit
    = wil::scope_exit([this]() { mBinaryLogFiles.clear(); });

  using unique_idlist
    = wil::unique_any<LPITEMIDLIST, decltype(&ILFree), ILFree>;

  if (mBinaryLogFiles.size() == 1) {
    CSVWriter::Write(
      std::move(mBinaryLogFiles.front()), outputPath, mCSVFramesPerRow);
    unique_idlist pidl;
    outputShellItem.query<IPersistIDList>()->GetIDList(pidl.put());
    SHOpenFolderAndSelectItems(pidl.get(), 0, nullptr, 0);
    return;
  }

  std::vector<std::filesystem::path> csvFiles;

  using unique_childid
    = wil::unique_any<PITEMID_CHILD, decltype(&ILFree), ILFree>;
  std::vector<unique_childid> childIDs;
  for (auto&& it: mBinaryLogFiles) {
    const auto itPath
      = (outputPath / it.GetLogFilePath().filename()).replace_extension(".csv");
    csvFiles.push_back(itPath);
    CSVWriter::Write(std::move(it), itPath, mCSVFramesPerRow);

    unique_idlist pidl;
    SHParseDisplayName(
      itPath.wstring().c_str(), nullptr, pidl.put(), 0, nullptr);
    childIDs.push_back(std::move(pidl));
  }

  unique_idlist folderPidl;
  outputShellItem.query<IPersistIDList>()->GetIDList(folderPidl.put());

  std::vector<PCITEMID_CHILD> childRawPtrs;
  for (auto&& it: childIDs) {
    childRawPtrs.push_back(it.get());
  }
  childRawPtrs.push_back(nullptr);
  childRawPtrs.push_back(nullptr);

  SHOpenFolderAndSelectItems(
    folderPidl.get(), childRawPtrs.size(), childRawPtrs.data(), 0);
}

static auto RoundUp(auto value, auto multiplier) {
  return ((value + multiplier) / multiplier) * multiplier;
}

template <class T>
struct MainWindow::LiveData::PlotPoint : ImPlotPoint {
  PlotPoint(const int idx, const T value)
    : ImPlotPoint(
        static_cast<double>(idx) / MainWindow::LiveData::ChartFPS,
        static_cast<double>(value)) {
  }
};

void MainWindow::LiveDataSection() {
  const ImGuiScoped::ID idScope {"Live data"};

  // "SpeedHigh" glyph
  ImGui::SeparatorText("\uec4aLive data");

  ImGui::Checkbox("Enable updates", &mLiveData.mEnabled);

  if (mLiveData.mEnabled) {
    this->UpdateLiveData();
  }

  const auto slowestFrameMicroseconds
    = std::ranges::max_element(mLiveData.mChartFrames, {}, [](const auto& it) {
        return it.mSincePreviousFrame.count();
      })->mSincePreviousFrame.count();
  const auto maxMicroseconds = std::clamp(
    static_cast<double>(RoundUp(slowestFrameMicroseconds, 1000)),
    0.0,
    1000000.0 / 15);
  const auto SetupMicrosecondsAxis = [maxMicroseconds](ImAxis axis) {
    ImPlot::SetupAxis(axis, "µs");
    ImPlot::SetupAxisLimits(axis, 0.0, maxMicroseconds, ImPlotCond_Always);
  };

  if (const auto plot = ImGuiScoped::ImPlot("FPS")) {
    ImPlot::SetupAxis(ImAxis_X1);

    ImPlot::SetupAxis(ImAxis_Y1, "hz");
    const auto fastestFrameMicroseconds
      = std::ranges::min_element(
          mLiveData.mChartFrames,
          {},
          [](const auto& it) { return it.mSincePreviousFrame.count(); })
          ->mSincePreviousFrame.count();
    const auto maxFPS = (fastestFrameMicroseconds == 0)
      ? 72.0
      : (1000000.0 / fastestFrameMicroseconds);
    ImPlot::SetupAxisLimits(
      ImAxis_Y1, 0, RoundUp(maxFPS, 5), ImPlotCond_Always);
    SetupMicrosecondsAxis(ImAxis_Y2);

    ImPlot::SetAxes(ImAxis_X1, ImAxis_Y1);
    ImPlot::PlotLineG(
      "FPS",
      [](int idx, void* user_data) -> ImPlotPoint {
        const auto& frame
          = static_cast<LiveData::ChartFrames*>(user_data)->at(idx);
        const auto interval = frame.mSincePreviousFrame.count();
        return LiveData::PlotPoint {
          idx,
          interval ? 1000000.0f / interval : 0,
        };
      },
      mLiveData.mChartFrames.data(),
      mLiveData.mChartFrames.size());

    ImPlot::SetAxes(ImAxis_X1, ImAxis_Y2);
    ImPlot::PlotLineG(
      "Frame Interval",
      [](int idx, void* user_data) -> ImPlotPoint {
        const auto& frame
          = static_cast<LiveData::ChartFrames*>(user_data)->at(idx);
        return LiveData::PlotPoint {
          idx,
          frame.mSincePreviousFrame.count(),
        };
      },
      mLiveData.mChartFrames.data(),
      mLiveData.mChartFrames.size());
  }
  constexpr auto plotZero = [](int idx, void* user_data) -> ImPlotPoint {
    return LiveData::PlotPoint {idx, 0};
  };
  constexpr auto plotAppCpu = [](int idx, void* user_data) -> ImPlotPoint {
    const auto& frame = static_cast<LiveData::ChartFrames*>(user_data)->at(idx);
    return LiveData::PlotPoint {
      idx,
      frame.mAppCpu.count(),
    };
  };
  constexpr auto plotWaitCpu = [](int idx, void* user_data) -> ImPlotPoint {
    const auto& frame = static_cast<LiveData::ChartFrames*>(user_data)->at(idx);
    return LiveData::PlotPoint {
      idx,
      frame.mWaitCpu.count(),
    };
  };
  constexpr auto plotRenderCpu = [](int idx, void* user_data) -> ImPlotPoint {
    const auto& frame = static_cast<LiveData::ChartFrames*>(user_data)->at(idx);
    return LiveData::PlotPoint {
      idx,
      frame.mRenderCpu.count(),
    };
  };
  constexpr auto plotRuntimeCpu = [](int idx, void* user_data) -> ImPlotPoint {
    const auto& frame = static_cast<LiveData::ChartFrames*>(user_data)->at(idx);
    return LiveData::PlotPoint {
      idx,
      frame.mRuntimeCpu.count(),
    };
  };

  if (const auto plot = ImGuiScoped::ImPlot("Frame Timings")) {
    ImPlot::SetupAxis(ImAxis_X1);
    SetupMicrosecondsAxis(ImAxis_Y1);
    ImPlot::SetAxes(ImAxis_X1, ImAxis_Y1);

    ImPlot::PushStyleVar(ImPlotStyleVar_FillAlpha, 0.25f);
    ImPlot::PlotShadedG(
      "App CPU",
      plotAppCpu,
      mLiveData.mChartFrames.data(),
      plotZero,
      mLiveData.mChartFrames.data(),
      mLiveData.mChartFrames.size());
    ImPlot::PlotShadedG(
      "Wait CPU",
      plotWaitCpu,
      mLiveData.mChartFrames.data(),
      plotZero,
      mLiveData.mChartFrames.data(),
      mLiveData.mChartFrames.size());
    ImPlot::PlotShadedG(
      "Render CPU",
      plotRenderCpu,
      mLiveData.mChartFrames.data(),
      plotZero,
      mLiveData.mChartFrames.data(),
      mLiveData.mChartFrames.size());
    ImPlot::PlotShadedG(
      "Runtime CPU",
      plotRuntimeCpu,
      mLiveData.mChartFrames.data(),
      plotZero,
      mLiveData.mChartFrames.data(),
      mLiveData.mChartFrames.size());
    ImPlot::PopStyleVar();

    ImPlot::PlotLineG(
      "App CPU",
      plotAppCpu,
      mLiveData.mChartFrames.data(),
      mLiveData.mChartFrames.size());
    ImPlot::PlotLineG(
      "Wait CPU",
      plotWaitCpu,
      mLiveData.mChartFrames.data(),
      mLiveData.mChartFrames.size());
    ImPlot::PlotLineG(
      "Render CPU",
      plotRenderCpu,
      mLiveData.mChartFrames.data(),
      mLiveData.mChartFrames.size());
    ImPlot::PlotLineG(
      "Runtime CPU",
      plotRuntimeCpu,
      mLiveData.mChartFrames.data(),
      mLiveData.mChartFrames.size());
  }
  ImPlot::ShowDemoWindow();
}

void MainWindow::UpdateLiveData() {
  if (mSHM.IsValid()) {
    if (mLiveData.mSHMFrameIndex == 0) {
      mLiveData.mSHMFrameIndex = mSHM->mFrameCount;
    }
    for (auto& i = mLiveData.mSHMFrameIndex; i < mSHM->mFrameCount; ++i) {
      const auto& frame = mSHM->GetFramePerformanceCounters(i);
      mLiveData.mLatestMetricsAt = frame.mEndFrameStop;
      mLiveData.mAggregator.Push(frame);
    }
  }

  const auto scNow = std::chrono::steady_clock::now();

  if (scNow - mLiveData.mLastChartFrameAt < LiveData::ChartInterval) {
    return;
  }
  mLiveData.mLastChartFrameAt = scNow;

  LARGE_INTEGER pcNow {};
  QueryPerformanceCounter(&pcNow);

  auto metrics = mLiveData.mAggregator.Flush();
  if (metrics) {
    mLiveData.mLatestMetrics = *metrics;
  } else if (
    gPCM.ToDuration(mLiveData.mLatestMetricsAt, pcNow)
    <= LiveData::ChartInterval * 5) {
    metrics = mLiveData.mLatestMetrics;
  } else {
    mLiveData.mChartFrames.push_back({});
    return;
  }

  if (metrics->mFrameCount == 0) {
    __debugbreak();
  }

  if (
    metrics->mSincePreviousFrame
    > std::chrono::seconds(LiveData::HistorySeconds)) {
    return;
  }

  mLiveData.mChartFrames.push_back(*metrics);
}
