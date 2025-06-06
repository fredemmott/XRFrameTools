// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT

#include "MainWindow.hpp"

#include <implot.h>
#include <nvapi.h>
#include <shellapi.h>
#include <shlobj_core.h>
#include <wil/com.h>
#include <wil/resource.h>
#include <wil/win32_helpers.h>

#include <magic_enum.hpp>
#include <ranges>

#include "CSVWriter.hpp"
#include "CheckHResult.hpp"
#include "FrameMetricsStore.hpp"
#include "ImGuiHelpers.hpp"
#include "ImStackedAreaPlotter.hpp"
#include "SHM.hpp"
#include "Version.hpp"
#include "Win32Utils.hpp"

static const auto gPCM = PerformanceCounterMath::CreateForLiveData();

static constexpr auto RoundUp(auto value, auto multiplier) {
  const auto floor = (static_cast<int64_t>(value) / multiplier) * multiplier;
  if (
    (value - floor) < value * std::numeric_limits<decltype(value)>::epsilon()) {
    return floor;
  }
  // int wraparound shenanigans. If you hit this, you probably want an explicit
  // 64-bit conversion in your inputs
  assert(floor + multiplier >= value);
  return floor + multiplier;
}

static void SetupMicrosecondsAxis(ImAxis axis, auto max) {
  ImPlot::SetupAxis(axis, "µs");
  ImPlot::SetupAxisLimits(
    axis, 0.0, RoundUp(max, 1000) + 1000, ImPlotCond_Always);
}

MainWindow::MainWindow(HINSTANCE instance)
  : Window(instance, L"XRFrameTools"),
    mBaseConfig(Config::GetUserDefaults(Config::Access::ReadWrite)),
    mLiveDataThread(
      std::bind_front(&MainWindow::UpdateLiveDataThreadEntry, this)) {
  mThisExecutable
    = std::filesystem::path {wil::QueryFullProcessImageNameW().get()};

  mCSVFramesPerRow = std::clamp<int>(
    static_cast<int>(
      wil::reg::try_get_value_dword(
        HKEY_CURRENT_USER, Config::RootSubkey, L"CSVFramesPerRow")
        .value_or(CSVWriter::DefaultFramesPerRow)),
    1,
    std::numeric_limits<int>::max());
}

MainWindow::~MainWindow() = default;

LiveData::LiveData() : mAggregator(gPCM) {
}
void MainWindow::UpdateLiveDataThreadEntry(const std::stop_token tok) {
  auto interruptEvent = mLiveData.mInterruptEvent.get();
  const std::stop_callback interrupt {
    tok, std::bind_front(&SetEvent, interruptEvent)};

  while (!tok.stop_requested()) {
    if (!mLiveData.mEnabled) {
      WaitForSingleObject(interruptEvent, INFINITE);
      continue;
    }

    {
      std::unique_lock lock(mLiveDataMutex);
      this->UpdateLiveData();
    }
    WaitForSingleObject(interruptEvent, 1000 / LiveData::ChartFPS);
  }
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
  if (ImGui::Button("\ue9f9Convert log files to CSV...")) {
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
  ImGui::LabelText("Log resolution", "%s", resolution.c_str());

  const auto executable = SingleValue(
    mBinaryLogFiles, "no log files", "varied", [](const auto& log) {
      return log.GetExecutablePath().string();
    });
  ImGui::LabelText("Application path", "%s", executable.c_str());

  enum class MetricsError {
    NoLogFiles,
    MissingData,
    MetricsVary,
  };
  struct Metrics {
    uint64_t mBytesPerFrame {};
    uint64_t mBytesPerHour {};
    uint32_t mAverageFPS;
    constexpr bool operator==(const Metrics&) const noexcept = default;
  };
  const auto metrics = SingleValue(
    mBinaryLogFiles,
    std::unexpected {MetricsError::NoLogFiles},
    std::unexpected {MetricsError::MetricsVary},
    [](const auto& log) -> std::expected<Metrics, MetricsError> {
      const auto footer = log.GetFileFooter();
      if (!footer) {
        return std::unexpected {MetricsError::MissingData};
      }
      if (!footer->mFrameCount) {
        return std::unexpected {MetricsError::MissingData};
      }

      const auto pcm = log.GetPerformanceCounterMath();
      const auto micros
        = pcm.ToDuration(footer->mFirstEndFrameTime, footer->mLastEndFrameTime)
            .count();
      const auto seconds = micros / 1e6;
      const auto hours = seconds / (60.0 * 60.0);

      return Metrics {
        .mBytesPerFrame = log.GetStreamSize() / footer->mFrameCount,
        .mBytesPerHour
        = static_cast<uint64_t>(std::llround(log.GetStreamSize() / hours)),
        .mAverageFPS
        = static_cast<uint32_t>(std::lround(footer->mFrameCount / seconds)),
      };
    });
  if (metrics.has_value()) {
    ImGui::LabelText(
      "Average FPS", "%s", std::to_string(metrics->mAverageFPS).c_str());
    ImGui::LabelText(
      "Disk usage per frame",
      "%s bytes",
      std::to_string(metrics->mBytesPerFrame).c_str());
    ImGui::LabelText(
      "Disk usage per hour",
      "%s MiB",
      std::to_string(metrics->mBytesPerHour / (1024 * 1024)).c_str());
  } else {
    using enum MetricsError;
    switch (metrics.error()) {
      case NoLogFiles:
        ImGui::LabelText("Average FPS", "no log files");
        ImGui::LabelText("Disk usage per frame", "no log files");
        ImGui::LabelText("Disk usage per hour", "no log files");
        break;
      case MissingData:
        ImGui::LabelText("Average FPS", "unknown");
        ImGui::LabelText("Disk usage per frame", "unknown");
        ImGui::LabelText("Disk usage per hour", "unknown");
        break;
      case MetricsVary:
        ImGui::LabelText("Average FPS", "varies");
        ImGui::LabelText("Disk usage per frame", "varies");
        ImGui::LabelText("Disk usage per hour", "varies");
        break;
    }
  }

  const auto validData = SingleValue(
    mBinaryLogFiles,
    std::unexpected {MetricsError::NoLogFiles},
    std::unexpected {MetricsError::MetricsVary},
    [](const auto& log) -> std::expected<uint64_t, MetricsError> {
      const auto footer = log.GetFileFooter();
      if (!footer) {
        return std::unexpected {MetricsError::MissingData};
      }

      return footer->mFrameCount;
    });
  if (validData) {
    std::string bits;
    for (auto&& [bit, name]:
         magic_enum::enum_entries<FramePerformanceCounters::ValidDataBits>()) {
      if (((*validData) & bit) == bit) {
        if (bits.empty()) {
          bits = name;
        } else {
          bits = std::format("{}, {}", bits, name);
        }
      }
    }
    ImGui::LabelText("Data sources", "%s", bits.c_str());
  } else {
    using enum MetricsError;
    switch (validData.error()) {
      case NoLogFiles:
        ImGui::LabelText("Data sources", "no log files");
      case MissingData:
        ImGui::LabelText("Data sources", "unknown");
      case MetricsVary:
        ImGui::LabelText("Data sources", "varied");
    }
  }

  if (ImGui::InputInt("Frames per CSV row (averaged)", &mCSVFramesPerRow)) {
    mCSVFramesPerRow
      = std::clamp(mCSVFramesPerRow, 1, std::numeric_limits<int>::max());
    wil::reg::set_value_dword_nothrow(
      HKEY_CURRENT_USER,
      Config::RootSubkey,
      L"CSVFramesPerRow",
      mCSVFramesPerRow);
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
  const auto tabItem = ImGuiScoped::TabItem("\ue81cPerformance logging");
  if (!tabItem) {
    return;
  }

  this->LoggingControls();

  ImGui::Separator();

  this->LogConversionControls();

  // "OpenFolderHorizontal"
  if (ImGui::Button("\ued25Open logs folder")) {
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

  if (const auto tabBar = ImGuiScoped::TabBar("##TabBar")) {
    this->LiveDataSection();
    this->LoggingSection();
    this->AboutSection();
  }
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
    {L"Logs files", L"*.XRFTBinLog"},
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

void MainWindow::PlotNVAPI() {
  const auto haveNVAPI = std::ranges::any_of(
    mLiveData.mChartFrames, [](const auto& frame) -> bool {
      return frame.mValidDataBits
        & std::to_underlying(FramePerformanceCounters::ValidDataBits::NVAPI);
    });
  if (!haveNVAPI) {
    return;
  }

  const auto plot = ImGuiScoped::ImPlot("GPU Throttling");
  if (!plot) {
    return;
  }

  double maxPState = 0;
  for (auto&& frame: mLiveData.mChartFrames) {
    if (frame.mGpuPStateMax > maxPState) {
      maxPState = frame.mGpuPStateMax;
    }
  }
  maxPState = std::clamp<double>(2, maxPState + 1, 16);
  uint8_t tickSize = 1;
  while (maxPState / tickSize > 8) {
    tickSize *= 2;
  }
  std::vector<std::string> pstateTicks;
  for (uint32_t i = 0; i < maxPState; i += tickSize) {
    pstateTicks.emplace_back(std::to_string(i));
  }
  std::vector<const char*> pstateTickCStrings;
  for (auto&& tick: pstateTicks) {
    pstateTickCStrings.emplace_back(tick.c_str());
  }

  ImPlot::SetupAxis(ImAxis_X1);
  ImPlot::SetupAxis(ImAxis_Y1);
  // 15 is the highest documented value; add a bit more so the line isn't at
  // the top if we ever get there
  ImPlot::SetupAxisLimits(ImAxis_Y1, 0, maxPState, ImPlotCond_Always);
  ImPlot::SetupAxisTicks(
    ImAxis_Y1,
    0,
    (pstateTicks.size() - 1) * tickSize,
    pstateTicks.size(),
    pstateTickCStrings.data());
  ImPlot::SetAxes(ImAxis_X1, ImAxis_Y1);

  ImPlot::PushStyleVar(
    ImPlotStyleVar_DigitalBitHeight, ImPlot::GetStyle().DigitalBitHeight * 2);
  ImPlot::PlotDigitalG(
    "Any Limit",
    &LiveData::PlotFrame<[](const FrameMetrics& frame) {
      return frame.mGpuPerformanceDecreaseReasons != 0;
    }>,
    mLiveData.mChartFrames.data(),
    mLiveData.mChartFrames.size());
  ImPlot::PlotDigitalG(
    "Thermal Limit",
    &LiveData::PlotFrame<[](const FrameMetrics& frame) {
      return (frame.mGpuPerformanceDecreaseReasons
              & NV_GPU_PERF_DECREASE_REASON_THERMAL_PROTECTION)
        != 0;
    }>,
    mLiveData.mChartFrames.data(),
    mLiveData.mChartFrames.size());
  ImPlot::PlotDigitalG(
    "Power Limit",
    &LiveData::PlotFrame<[](const FrameMetrics& frame) {
      return (frame.mGpuPerformanceDecreaseReasons
              & (NV_GPU_PERF_DECREASE_REASON_POWER_CONTROL | NV_GPU_PERF_DECREASE_REASON_AC_BATT | NV_GPU_PERF_DECREASE_REASON_INSUFFICIENT_POWER))
        != 0;
    }>,
    mLiveData.mChartFrames.data(),
    mLiveData.mChartFrames.size());
  ImPlot::PlotDigitalG(
    "API Limit",
    &LiveData::PlotFrame<[](const FrameMetrics& frame) {
      return (frame.mGpuPerformanceDecreaseReasons
              & NV_GPU_PERF_DECREASE_REASON_API_TRIGGERED)
        != 0;
    }>,
    mLiveData.mChartFrames.data(),
    mLiveData.mChartFrames.size());
  ImPlot::PopStyleVar();

  ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, ImGui::GetFont()->Scale * 3);
  ImPlot::PlotLineG(
    "Lowest P-State",
    &LiveData::PlotFrame<&FrameMetrics::mGpuPStateMin>,
    mLiveData.mChartFrames.data(),
    mLiveData.mChartFrames.size());
  ImPlot::PlotLineG(
    "Highest P-State",
    &LiveData::PlotFrame<&FrameMetrics::mGpuPStateMax>,
    mLiveData.mChartFrames.data(),
    mLiveData.mChartFrames.size());
  ImPlot::PopStyleVar();
}

void MainWindow::PlotFramerate(const double maxMicroseconds) {
  const auto plot = ImGuiScoped::ImPlot("FPS");
  if (!plot) {
    return;
  }

  ImPlot::SetupAxis(ImAxis_X1);

  ImPlot::SetupAxis(ImAxis_Y1, "hz");

  std::optional<double> minInterval;
  for (auto&& frame: mLiveData.mChartFrames) {
    const auto interval = frame.mSincePreviousFrame.count();
    if (!interval) {
      continue;
    }
    if (
      interval
      < minInterval.value_or(std::numeric_limits<double>::infinity())) {
      minInterval = interval;
    }
  }

  const auto maxFPS
    = minInterval.has_value() ? (1e6 / minInterval.value()) : 72.0;
  ImPlot::SetupAxisLimits(ImAxis_Y1, 0, maxFPS, ImPlotCond_Always);
  SetupMicrosecondsAxis(ImAxis_Y2, maxMicroseconds);

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

void MainWindow::PlotFrameTimings(const double maxMicroseconds) {
  if (const auto plot = ImGuiScoped::ImPlot("Frame Timings")) {
    ImPlot::SetupAxis(ImAxis_X1);
    SetupMicrosecondsAxis(ImAxis_Y1, maxMicroseconds);
    ImPlot::SetAxes(ImAxis_X1, ImAxis_Y1);

    ImStackedAreaPlotter sap {mFrameTimingPlotKind};
    sap.Plot(
      "Begin CPU",
      &LiveData::PlotMicroseconds<&FrameMetrics::mBeginFrameCpu>,
      mLiveData.mChartFrames.data(),
      mLiveData.mChartFrames.size());
    sap.Plot(
      "App CPU",
      &LiveData::PlotMicroseconds<&FrameMetrics::mAppCpu>,
      mLiveData.mChartFrames.data(),
      mLiveData.mChartFrames.size());
    sap.Plot(
      "Render CPU",
      &LiveData::PlotMicroseconds<&FrameMetrics::mRenderCpu>,
      mLiveData.mChartFrames.data(),
      mLiveData.mChartFrames.size());
    sap.Plot(
      "Submit CPU",
      &LiveData::PlotMicroseconds<&FrameMetrics::mEndFrameCpu>,
      mLiveData.mChartFrames.data(),
      mLiveData.mChartFrames.size());
    sap.HideNextItem(ImPlotCond_Once);
    sap.Plot(
      "Wait CPU",
      &LiveData::PlotMicroseconds<&FrameMetrics::mWaitFrameCpu>,
      mLiveData.mChartFrames.data(),
      mLiveData.mChartFrames.size());

    ImPlot::PlotLineG(
      "Render GPU",
      &LiveData::PlotMicroseconds<&FrameMetrics::mRenderGpu>,
      mLiveData.mChartFrames.data(),
      mLiveData.mChartFrames.size());

    ImPlot::HideNextItem(ImPlotCond_Once);
    ImPlot::PlotLineG(
      "Frame Interval",
      &LiveData::PlotMicroseconds<&FrameMetrics::mSincePreviousFrame>,
      mLiveData.mChartFrames.data(),
      mLiveData.mChartFrames.size());
  }

  using PlotKind = ImStackedAreaPlotter::Kind;
  if (ImGui::RadioButton(
        "Stacked area", mFrameTimingPlotKind == PlotKind::StackedArea)) {
    mFrameTimingPlotKind = PlotKind::StackedArea;
  }
  ImGui::SameLine();
  if (ImGui::RadioButton("Lines", mFrameTimingPlotKind == PlotKind::Lines)) {
    mFrameTimingPlotKind = PlotKind::Lines;
  }
}

void MainWindow::PlotVideoMemory() {
  const auto plot = ImGuiScoped::ImPlot("Video Memory");
  if (!plot) {
    return;
  }
  const auto max = std::ranges::max_element(
    mLiveData.mChartFrames, {}, [](const FrameMetrics& frame) {
      return std::max(
        frame.mVideoMemoryInfo.AvailableForReservation,
        frame.mVideoMemoryInfo.Budget);
    });

  const auto vramAxisLimit = RoundUp(
                               std::max(
                                 max->mVideoMemoryInfo.AvailableForReservation,
                                 max->mVideoMemoryInfo.Budget),
                               5ui64 * 1024 * 1024 * 1024)

    / (1024 * 1024);
  std::vector<std::string> labels;
  uint64_t tickSize = 1024;
  while (vramAxisLimit / tickSize > 8) {
    tickSize *= 2;
  }
  for (uint64_t i = 0; i < vramAxisLimit; i += tickSize) {
    labels.push_back(std::format("{}", i));
  }
  std::vector<const char*> labelCStrings;
  for (auto&& label: labels) {
    labelCStrings.push_back(label.c_str());
  }

  ImPlot::SetupAxis(ImAxis_X1);
  ImPlot::SetupAxis(ImAxis_Y1, "mb");
  ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, vramAxisLimit, ImPlotCond_Always);
  ImPlot::SetupAxisTicks(
    ImAxis_Y1,
    0.0,
    (labels.size() - 1) * tickSize,
    labels.size(),
    labelCStrings.data());
  ImPlot::SetAxes(ImAxis_X1, ImAxis_Y1);

  ImPlot::PlotLineG(
    "Current Usage",
    &LiveData::PlotVideoMemory<&DXGI_QUERY_VIDEO_MEMORY_INFO::CurrentUsage>,
    mLiveData.mChartFrames.data(),
    mLiveData.mChartFrames.size());
  ImPlot::PlotLineG(
    "Budget",
    &LiveData::PlotVideoMemory<&DXGI_QUERY_VIDEO_MEMORY_INFO::Budget>,
    mLiveData.mChartFrames.data(),
    mLiveData.mChartFrames.size());
  ImPlot::PlotLineG(
    "Current Reservation",
    &LiveData::PlotVideoMemory<
      &DXGI_QUERY_VIDEO_MEMORY_INFO::CurrentReservation>,
    mLiveData.mChartFrames.data(),
    mLiveData.mChartFrames.size());
  ImPlot::PlotLineG(
    "Available for Reservation",
    &LiveData::PlotVideoMemory<
      &DXGI_QUERY_VIDEO_MEMORY_INFO::AvailableForReservation>,
    mLiveData.mChartFrames.data(),
    mLiveData.mChartFrames.size());
}

void MainWindow::LiveDataSection() {
  std::unique_lock lock(mLiveDataMutex);

  // "SpeedHigh" glyph
  const auto tabItem = ImGuiScoped::TabItem("\uec4aLive data");
  if (!tabItem) {
    return;
  }

  {
    const auto disabled = ImGuiScoped::DisabledIf(mLiveData.mEnabled);
    // "Play"
    if (ImGui::Button("\ue768")) {
      mLiveData.mEnabled = true;
      SetEvent(mLiveData.mInterruptEvent.get());
    }
  }
  ImGui::SameLine();
  {
    const auto enabled = ImGuiScoped::EnabledIf(mLiveData.mEnabled);
    // "Pause"
    if (ImGui::Button("\ue769")) {
      mLiveData.mEnabled = false;
    }
  }
  ImGui::SameLine();
  // "Delete"
  if (ImGui::Button("\ue74d")) {
    mLiveApp = {};
    mLiveData.mSHMFrameIndex = 0;
    mLiveData.mAggregator.Reset();
    mLiveData.mChartFrames = {LiveData::BufferSize};
  }

  if (mSHM.IsValid() && mSHM->mWriterProcessID != mLiveApp.mProcessID) {
    mLiveApp = {.mProcessID = mSHM->mWriterProcessID};
    mLiveData.mSHMFrameIndex = 0;
  }

  wil::unique_handle process {OpenProcess(
    PROCESS_QUERY_LIMITED_INFORMATION, FALSE, mSHM->mWriterProcessID)};
  if (process) {
    mLiveApp.mExecutablePath
      = wil::QueryFullProcessImageNameW(process.get()).get();
    BOOL is32Bit = FALSE;
    if (IsWow64Process(process.get(), &is32Bit)) {
      mLiveApp.mProcessBitness = is32Bit ? 32 : 64;
    }
  }

  ImGui::SameLine();
  if (mLiveApp.mExecutablePath.empty()) {
    ImGui::TextDisabled("No current OpenXR application detected");
  } else {
    ImGui::TextDisabled(
      "Showing PID %ld: %s (%s)",
      mLiveApp.mProcessID,
      mLiveApp.mExecutablePath.string().c_str(),
      (mLiveApp.mProcessBitness.has_value()
         ? std::format("{}-bit", mLiveApp.mProcessBitness.value())
         : "unknown architecture")
        .c_str());
  }

  const auto slowestFrameMicroseconds
    = std::ranges::max_element(mLiveData.mChartFrames, {}, [](const auto& it) {
        return it.mSincePreviousFrame.count();
      })->mSincePreviousFrame.count();
  const auto maxMicroseconds = std::clamp(
    static_cast<double>(RoundUp(slowestFrameMicroseconds, 1000)),
    0.0,
    1000000.0 / 15);

  this->PlotFramerate(maxMicroseconds);
  this->PlotFrameTimings(maxMicroseconds);
  this->PlotSystemFrequencies();
  this->PlotVideoMemory();
  this->PlotNVAPI();
}

void MainWindow::PlotSystemFrequencies() {
  const auto plot = ImGuiScoped::ImPlot("Frequencies");
  if (!plot) {
    return;
  }
  ImPlot::SetupAxis(ImAxis_X1);
  ImPlot::SetupAxis(ImAxis_Y1, "MHz", ImPlotAxisFlags_AutoFit);
  ImPlot::SetAxes(ImAxis_X1, ImAxis_Y1);

  ImPlot::PlotLineG(
    "GPU Min",
    &LiveData::PlotFrame<&FrameMetrics::mGpuGraphicsKHzMin, 1000.0>,
    mLiveData.mChartFrames.data(),
    mLiveData.mChartFrames.size());
  ImPlot::PlotLineG(
    "GPU Max",
    &LiveData::PlotFrame<&FrameMetrics::mGpuGraphicsKHzMax, 1000.0>,
    mLiveData.mChartFrames.data(),
    mLiveData.mChartFrames.size());
  ImPlot::PlotLineG(
    "VRAM Clock Min",
    &LiveData::PlotFrame<&FrameMetrics::mGpuMemoryKHzMin, 1000.0>,
    mLiveData.mChartFrames.data(),
    mLiveData.mChartFrames.size());
  ImPlot::PlotLineG(
    "VRAM Clock Max",
    &LiveData::PlotFrame<&FrameMetrics::mGpuMemoryKHzMax, 1000.0>,
    mLiveData.mChartFrames.data(),
    mLiveData.mChartFrames.size());
}

void MainWindow::AboutSection() {
  const ImGuiScoped::ID idScope {"About"};

  const auto tabItem = ImGuiScoped::TabItem("\ue897About");
  if (!tabItem) {
    return;
  }

  if constexpr (!Version::IsStableRelease) {
    if constexpr (!Version::IsTaggedBuild) {
      ImGui::TextColored({1.0, 0.0, 0.0, 1.0}, "DEVELOPMENT BUILD");
    } else {
      ImGui::TextColored({1.0, 0.0, 0.0, 1.0}, "Public Test Version");
    }
  }
  ImGui::Text(
    R"---(XRFrameTool v%s
Layer version %s

Copyright © 2024 Fred Emmott

XRFrameTools is distributed under the MIT license; it contains third-party components, distributed under their own terms.
)---",
    Version::SemVer,
    std::to_string(Version::ApiLayerImplementationVersion).c_str());
  if (ImGui::TextLink("License details")) {
    ShellExecuteW(
      GetHWND(),
      L"open",
      (mThisExecutable.parent_path().parent_path() / "share/doc")
        .wstring()
        .c_str(),
      nullptr,
      nullptr,
      SW_SHOW);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
  }
}

void MainWindow::UpdateLiveData() {
  if (mSHM.IsValid()) {
    if (mLiveData.mSHMFrameIndex == 0) {
      mLiveData.mSHMFrameIndex = mSHM->mFrameCount;
    }
    for (auto& i = mLiveData.mSHMFrameIndex; i < mSHM->mFrameCount; ++i) {
      const auto& frame = mSHM->GetFramePerformanceCounters(i);
      mLiveData.mLatestMetricsAt = frame.mCore.mEndFrameStop;
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
    return;
  }

  if (
    metrics->mSincePreviousFrame
    > std::chrono::seconds(LiveData::HistorySeconds)) {
    return;
  }

  mLiveData.mChartFrames.push_back(*metrics);
}
