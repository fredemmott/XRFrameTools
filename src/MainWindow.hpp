// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT
#pragma once

#include <vector>

#include "BinaryLogReader.hpp"
#include "CSVWriter.hpp"
#include "Window.hpp"

class MainWindow final : public Window {
 public:
  explicit MainWindow(const HINSTANCE instance);
  ~MainWindow() override;

 protected:
  using Window::Window;

  void RenderContent() override;

 private:
  int mCSVFramesPerRow {CSVWriter::DefaultFramesPerRow};
  std::vector<BinaryLogReader> mBinaryLogFiles;
  void PickBinaryLogFiles();
  void ConvertBinaryLogFiles();
};