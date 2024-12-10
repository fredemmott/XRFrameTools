include_guard(DIRECTORY)

add_library(
  BinaryLogReader
  STATIC
  BinaryLog.hpp
  BinaryLogReader.cpp BinaryLogReader.hpp
)
target_link_libraries(
  BinaryLogReader
  PUBLIC
  WIL::WIL
)
