set(GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/include")
set(ABI_KEY_HPP "${GENERATED_INCLUDE_DIR}/XRFrameTools/ABIKey.hpp")
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/gen/ABIKey.in.hpp"
  "${ABI_KEY_HPP}"
  @ONLY
  NEWLINE_STYLE UNIX
)

add_library(ABIKey INTERFACE)
target_include_directories(ABIKey INTERFACE "${GENERATED_INCLUDE_DIR}")

include(BinaryLogReader.cmake)
include(BinaryLogWriter.cmake)
include(Config.cmake)
include(CSVWriter.cmake)
include(D3d11GpuTimer.cmake)
include(FrameMetrics.cmake)
include(PerformanceCounters.cmake)
include(SHMReader.cmake)
include(SHMWriter.cmake)
include(Version.cmake)
include(Win32Utils.cmake)