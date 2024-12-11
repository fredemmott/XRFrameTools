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
  ImGui::SeparatorText("\ue81c Logs");

  // "OpenFolderHorizontal" glyph
  if (ImGui::Button("\ued25 Open...")) {
    this->PickBinaryLogFiles();
  }
  ImGui::BeginDisabled(mBinaryLogFiles.empty());
  const auto endDisabled = wil::scope_exit(&ImGui::EndDisabled);

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