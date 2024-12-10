include_guard(DIRECTORY)

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
)