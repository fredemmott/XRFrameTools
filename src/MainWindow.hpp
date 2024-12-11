// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT
#pragma once

#include <vector>

#include "BinaryLogReader.hpp"
#include "Window.hpp"

class MainWindow final : public Window {
 public:
  explicit MainWindow(const HINSTANCE instance);
  ~MainWindow();

 protected:
  using Window::Window;

  void RenderContent() override;

 private:
  int mCSVFramesPerRow {10};
  std::vector<BinaryLogReader> mBinaryLogFiles;
  void PickBinaryLogFiles();
};