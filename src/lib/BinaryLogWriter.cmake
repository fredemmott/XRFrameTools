include_guard(DIRECTORY)

include(Win32Utils.cmake)

add_library(
  BinaryLogWriter
  STATIC
  BinaryLog.hpp
  BinaryLogWriter.cpp BinaryLogWriter.hpp
)
target_link_libraries(
  BinaryLogWriter
  PUBLIC
  WIL::WIL
  PRIVATE
  Win32Utils
)