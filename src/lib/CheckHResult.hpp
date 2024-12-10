// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT
#pragma once

#include <Windows.h>

#include <bit>
#include <format>
#include <source_location>

[[noreturn]]
inline void ThrowHResult(
  HRESULT result,
  std::string_view message = "HRESULT failed",
  const std::source_location& caller = std::source_location::current()) {
  const std::error_code ec {result, std::system_category()};
  const auto formatted = std::format(
    "{}: {:#010x} @ {} - {}:{}:{} - {}",
    message,
    std::bit_cast<uint32_t>(result),
    caller.function_name(),
    caller.file_name(),
    caller.line(),
    caller.column(),
    ec.message());
  OutputDebugStringA(std::format("XRFrameTool: {}", formatted).c_str());
  throw std::system_error(ec, formatted);
}

inline void CheckHResult(
  HRESULT result,
  std::string_view message = "HRESULT failed",
  const std::source_location& caller = std::source_location::current()) {
  if (SUCCEEDED(result)) [[likely]] {
    return;
  }

  ThrowHResult(result, message, caller);
}