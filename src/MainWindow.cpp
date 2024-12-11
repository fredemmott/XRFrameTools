// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT

#include "MainWindow.hpp"

#include <shlobj_core.h>
#include <wil/com.h>
#include <wil/resource.h>
#include <wil/win32_helpers.h>

#include <magic_enum.hpp>

#include "CSVWriter.hpp"
#include "CheckHResult.hpp"
#include "ImGuiHelpers.hpp"
#include "Win32Utils.hpp"

MainWindow::MainWindow(HINSTANCE instance)
  : Window(instance, L"XRFrameTools"),
    mBaseConfig(Config::GetUserDefaults(Config::Access::ReadWrite)) {
}

MainWindow::~MainWindow() {
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

  // "History" glyph
  ImGui::SeparatorText("\ue81c Logs");

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
  if (ImGui::BeginPopup("EnablePopup")) {
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
    ImGui::EndPopup();
  }

  ImGui::ShowDemoWindow();

  // "OpenFolderHorizontal" glyph
  if (ImGui::Button("\ued25 Open...")) {
    this->PickBinaryLogFiles();
  }
  const ImGuiScoped::DisabledIf noLogFiles(mBinaryLogFiles.empty());

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

  // "Processing" Glyph
  if (ImGui::Button("\ue9f9 Convert to CSV...")) {
    this->ConvertBinaryLogFiles();
  }
}
void MainWindow::PickBinaryLogFiles() {
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
    {L"Binary logs", L"*.XRFrameToolsBinLog"},
  };
  picker->SetFileTypes(std::size(fileTypes), fileTypes);
  if (picker->Show(this->GetHWND()) != S_OK) {
    return;
  }

  wil::com_ptr<IShellItemArray> items;
  CheckHResult(picker->GetResults(items.put()));

  DWORD count {};
  CheckHResult(items->GetCount(&count));
  if (count == 0) {
    return;
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
      return;
    }
  }

  mBinaryLogFiles = std::move(readers);
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

  if (mBinaryLogFiles.size() == 1) {
    CSVWriter::Write(
      std::move(mBinaryLogFiles.front()), outputPath, mCSVFramesPerRow);
  } else {
    std::vector<std::filesystem::path> csvFiles;
    for (auto&& it: mBinaryLogFiles) {
      const auto itPath
        = (outputPath / it.GetLogFilePath()).replace_extension(".csv");
      csvFiles.push_back(itPath);
      CSVWriter::Write(std::move(it), itPath, mCSVFramesPerRow);
    }
  }

  LPITEMIDLIST pidl {};
  outputShellItem.query<IPersistIDList>()->GetIDList(&pidl);
  SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
  ILFree(pidl);

  mBinaryLogFiles.clear();
}