include_guard(DIRECTORY)

include(BinaryLogReader.cmake)
include(Win32Utils.cmake)

add_library(
  CSVWriter
  STATIC
  CSVWriter.cpp CSVWriter.hpp
)
target_link_libraries(
  CSVWriter
  PUBLIC
  BinaryLogReader
  PRIVATE
  nvapi
  FrameMetrics
  Win32Utils
)