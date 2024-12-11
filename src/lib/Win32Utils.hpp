// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT
#pragma once

#include <Windows.h>

#include <filesystem>
#include <print>
#include <utility>

#include "detail/GuidParser.hpp"

std::filesystem::path GetKnownFolderPath(const GUID& folderID);

template <detail::CompileTimeStringHelper CTS>
consteval GUID operator"" _guid() {
  return []<std::size_t... Index>(std::index_sequence<Index...>) {
    // Construct an instance so we get the nice compile error if it's the wrong
    // size
    return detail::compile_time_guid::GuidParser<CTS.mValue[Index]...> {}
      .Parse();
  }(std::make_index_sequence<sizeof(CTS.mValue)> {});
}

namespace win32 {
// like std::println, but writing to a HANDLE
template <class... Args>
void println(
  HANDLE handle,
  std::format_string<Args...> format,
  Args&&... args) {
  const auto buffer = std::format(format, std::forward<Args>(args)...) + '\n';
  DWORD bytesWritten = {};
  while (bytesWritten < buffer.size()) {
    const auto remaining = buffer.size() - bytesWritten;
    DWORD bytesWrittenThisBatch {};
    const auto result = WriteFile(
      handle,
      buffer.data() + bytesWritten,
      remaining,
      &bytesWrittenThisBatch,
      nullptr);
    if (!SUCCEEDED(result)) {
      throw std::system_error(result, std::system_category());
    }
    bytesWritten += bytesWrittenThisBatch;
  }
}
}// namespace win32

template <class... Args>
void dprint(std::format_string<Args...> format, Args&&... args) {
  const auto inner = std::format(format, std::forward<Args>(args)...);
  const auto outer = std::format("XRFrameTools: {}\n", inner);
  OutputDebugStringA(outer.data());
}