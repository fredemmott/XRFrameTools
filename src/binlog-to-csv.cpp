// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT

#include <Windows.h>
#include <wil/filesystem.h>

#include <BinaryLogReader.hpp>
#include <expected>
#include <functional>
#include <magic_enum.hpp>
#include <print>

#include "CSVWriter.hpp"
#include "Win32Utils.hpp"

namespace {

struct Arguments {
  std::filesystem::path mInput;
  std::filesystem::path mOutput;
  size_t mAggregateBatch {CSVWriter::DefaultFramesPerRow};
};

void ShowUsage(std::FILE* stream, std::string_view exe) {
  std::println(
    stream,
    "USAGE: {} [--help] [--output PATH] [--aggregate COUNT] INPUT_PATH\n\n"
    "  --aggregate COUNT\n\n"
    "    number of frames to include in each row; default {}",
    std::filesystem::path {exe}.stem().string(),
    CSVWriter::DefaultFramesPerRow);
}

std::optional<std::filesystem::path> ArgToInputPath(std::string_view arg) {
  const std::filesystem::path path {arg};
  try {
    if (std::filesystem::is_regular_file(path)) {
      return std::filesystem::canonical(path);
    }
    std::println(stderr, "`{}` is not a regular file", arg);
    return std::nullopt;
  } catch (const std::filesystem::filesystem_error& ec) {
    std::println(stderr, "`{}` is not accessible: {}", arg, ec.what());
    return std::nullopt;
  }
}

[[nodiscard]]
std::expected<Arguments, int> ParseArguments(int argc, char* argv[]) {
  Arguments ret;
  const std::string_view thisExe {argv[0]};

  for (size_t i = 1; i < argc; ++i) {
    const std::string_view arg {argv[i]};
    // Allow `--help` anywhere
    if (arg == "--help") {
      ShowUsage(stdout, thisExe);
      return std::unexpected {EXIT_SUCCESS};
    }

    // OK, almost anywhere :)
    if (arg == "--") {
      break;
    }
  }

  bool parse = true;// set to false after seeing `--` by itself
  for (size_t i = 1; i < argc; ++i) {
    const std::string_view arg {argv[i]};
    // --help is handled above
    if (parse && arg == "--aggregate") {
      ++i;
      if (i >= argc) {
        std::println(stderr, "--aggregate requires a value");
        return std::unexpected {EXIT_FAILURE};
      }
      std::string stringValue {argv[i]};
      try {
        const auto value = std::stoi(stringValue);
        if (value < 1) {
          std::println(stderr, "--aggregate value must be at least 1");
          return std::unexpected {EXIT_FAILURE};
        }
        ret.mAggregateBatch = static_cast<size_t>(value);
        continue;
      } catch (...) {
        std::println(stderr, "--aggregate value must be a number");
        return std::unexpected {EXIT_FAILURE};
      }
    }

    if (parse && arg == "--output") {
      ++i;
      if (i >= argc) {
        std::println(stderr, "--output requires a value");
        return std::unexpected {EXIT_FAILURE};
      }
      ret.mOutput = {argv[i]};
      continue;
    }

    if (parse && arg == "--") {
      parse = false;
      continue;
    }

    if (parse && arg.starts_with("-")) {
      ShowUsage(stderr, thisExe);
      return std::unexpected {EXIT_FAILURE};
    }

    if (!ret.mInput.empty()) {
      std::println(
        stderr,
        "Multiple input files specified:\n  {}\n  {}",
        ret.mInput.string(),
        arg);
      return std::unexpected {EXIT_FAILURE};
    }

    const auto path = ArgToInputPath(arg);
    if (!path) {
      return std::unexpected {EXIT_FAILURE};
    }
    ret.mInput = *path;
  }

  if (ret.mInput.empty()) {
    ShowUsage(stderr, thisExe);
    return std::unexpected {EXIT_FAILURE};
  }

  return ret;
}

}// namespace

int main(int argc, char** argv) {
#ifndef NDEBUG
  if (GetACP() != CP_UTF8) {
    std::println(
      stderr,
      "BUILD ERROR: process code page should be forced to UTF-8 via manifest");
    return EXIT_FAILURE;
  }
#endif
  const auto startTime = std::chrono::steady_clock::now();

  const auto args = ParseArguments(argc, argv);
  if (!args) {
    return args.error();
  }

  auto reader = BinaryLogReader::Create(args->mInput);
  if (!reader) {
    std::println(
      stderr,
      "Opening binary log failed: {}",
      magic_enum::enum_name(reader.error().GetCode()));
    return EXIT_FAILURE;
  }

  const auto stderrHandle = GetStdHandle(STD_ERROR_HANDLE);
  DWORD stderrMode {};
  GetConsoleMode(stderrHandle, &stderrMode);
  const auto restoreConsoleMode
    = wil::scope_exit([=] { SetConsoleMode(stderrHandle, stderrMode); });
  SetConsoleMode(
    stderrHandle,
    stderrMode | ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

  const auto pcm = reader->GetPerformanceCounterMath();

  std::println(
    stderr,
    "\x1b[1;7mLog resolution:\x1b[22m     {} ticks per second\x1b[m",
    pcm.GetResolution().QuadPart);
  std::println(
    stderr,
    "\x1b[1;7mOpenXR application:\x1b[22m {}\x1b[m",
    reader->GetExecutablePath().string());

  wil::unique_hfile outputFile;
  if (!args->mOutput.empty()) {
    const auto path = std::filesystem::absolute(args->mOutput);
    try {
      if (!std::filesystem::is_directory(path.parent_path())) {
        std::filesystem::create_directories(path.parent_path());
      }
    } catch (const std::filesystem::filesystem_error& ec) {
      std::println(
        stderr,
        "Couldn't create `{}`: {}",
        args->mOutput.parent_path().string(),
        ec.what());
      return EXIT_FAILURE;
    }

    auto [handle, error] = wil::try_open_or_truncate_existing_file(
      path.wstring().c_str(), GENERIC_WRITE);
    if (!handle) {
      const std::error_code ec {
        HRESULT_FROM_WIN32(error), std::system_category()};
      std::println(stderr, "Couldn't open output file `{}`", ec.message());
      return EXIT_FAILURE;
    }
    outputFile = std::move(handle);
  }

  const auto out
    = outputFile ? outputFile.get() : GetStdHandle(STD_OUTPUT_HANDLE);
  const auto result
    = CSVWriter::Write(std::move(reader).value(), out, args->mAggregateBatch);

  if (result.mFrameCount == 0) {
    std::println(stderr, "❌ log doesn't contain any frames");
    return EXIT_FAILURE;
  }

  std::println(
    stderr,
    "✅ Wrote {} rows covering {} frames",
    result.mRowCount,
    result.mFrameCount);

  if (result.mLogDuration) {
    std::println(
      stderr,
      "⏱️ {:.03f} seconds recorded in log",
      result.mLogDuration->count() / 1000.0f);
  }

  const auto conversionTime
    = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - startTime);
  std::println(
    stderr, "⚙️ exported CSV in {:.03f}s", conversionTime.count() / 1000.0f);
  return EXIT_SUCCESS;
}