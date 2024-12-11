// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT

#include "MainWindow.hpp"

#include <shlobj_core.h>
#include <wil/com.h>
#include <wil/resource.h>
#include <wil/win32_helpers.h>

#include <magic_enum.hpp>

#include "CheckHResult.hpp"
#include "Win32Utils.hpp"

MainWindow::MainWindow(HINSTANCE instance) : Window(instance, L"XRFrameTools") {
}

MainWindow::~MainWindow() {
}

void MainWindow::RenderContent() {
  // Unicode escapes are gylphs from the Windows icon fonts:
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
  ImGui::SeparatorText("\ue81c Binary Logs");

  // "OpenFolderHorizontal" glyph
  if (ImGui::Button("\ued25 Open...")) {
    this->PickBinaryLogFiles();
  }
  ImGui::BeginDisabled(mBinaryLogFiles.empty());
  const auto endDisabled = wil::scope_exit(&ImGui::EndDisabled);

  if (mBinaryLogFiles.empty()) {
    ImGui::LabelText("Log resolution", "no log files");
  } else {
    constexpr auto getResolution = [](const BinaryLogReader& reader) {
      return reader.GetPerformanceCounterMath().GetResolution().QuadPart;
    };
    const auto first = getResolution(mBinaryLogFiles.front());
    const auto it = std::ranges::find_if(
      mBinaryLogFiles, [first, getResolution](const BinaryLogReader& reader) {
        return getResolution(reader) != first;
      });
    if (it == mBinaryLogFiles.end()) {
      ImGui::LabelText(
        "Log resolution", "%s", std::format("{}hz", first).c_str());
    } else {
      ImGui::LabelText("Log resolution", "varied");
    }
  }

  if (mBinaryLogFiles.empty()) {
    ImGui::LabelText("Application", "no log files");
  } else {
    const auto first = mBinaryLogFiles.front().GetExecutablePath();
    const auto it = std::ranges::find_if(
      mBinaryLogFiles, [first](const BinaryLogReader& reader) {
        return reader.GetExecutablePath() != first;
      });
    if (it == mBinaryLogFiles.end()) {
      ImGui::LabelText("Application", "%s", first.string().c_str());
    } else {
      ImGui::LabelText("Application", "varied");
    }
  }

  int aggregateFrames = 10;
  if (ImGui::InputInt("Frames per row (averaged)", &mCSVFramesPerRow)) {
    if (mCSVFramesPerRow < 1) {
      mCSVFramesPerRow = 1;
    }
  }

  // "Processing" Glyph
  if (ImGui::Button("\ue9f9 Convert to CSV...")) {
    // TODO
  }
}
void MainWindow::PickBinaryLogFiles() {
  // used for remembering location/preferences
  constexpr GUID BinaryLogsFilePicker
    = "{f09453d5-0bb2-4c09-971d-b8c4fa45c2c3}"_guid;

  const auto picker
    = wil::CoCreateInstance<IFileOpenDialog>(CLSID_FileOpenDialog);
  CheckHResult(picker->SetClientGuid(BinaryLogsFilePicker));
  picker->SetTitle(L"Open binary log files");
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
      "Error opening binary log file",
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