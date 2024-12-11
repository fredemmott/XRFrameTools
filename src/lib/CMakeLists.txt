set(GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/include")
set(ABI_KEY_HPP "${GENERATED_INCLUDE_DIR}/XRFrameTools/ABIKey.hpp")
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/gen/ABIKey.in.hpp"
  "${ABI_KEY_HPP}"
  @ONLY
  NEWLINE_STYLE UNIX
)

include(BinaryLogReader.cmake)
include(BinaryLogWriter.cmake)
include(CSVWriter.cmake)
include(FrameMetrics.cmake)
include(PerformanceCounters.cmake)
include(SHMReader.cmake)
include(SHMWriter.cmake)
include(Win32Utils.cmake)
