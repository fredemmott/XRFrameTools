// Copyright 2024 Fred Emmott <fred@fredemmott.com>
// SPDX-License-Identifier: MIT

#include <string>
#include <Windows.h>
#include <wil/resource.h>
#include <print>

int main(int argc, char** argv) {
  wil::unique_hmodule dll { LoadLibraryA(argv[1]) };
  if (!dll) {
    std::println(stderr, "ERROR: Failed to load `{}`", argv[1]);
    return EXIT_FAILURE;
  }
  const auto entryPoint = GetProcAddress(dll.get(), "xrNegotiateLoaderApiLayerInterface");
  if (!entryPoint) {
    std::println(stderr, "ERROR: Failed to find `xrNegotiateApiLayerInterface` in `{}`", argv[1]);
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
