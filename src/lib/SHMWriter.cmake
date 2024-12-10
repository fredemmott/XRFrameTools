include_guard(DIRECTORY)

include(SHMClient.cmake)

add_library(
  SHMWriter
  STATIC
  SHMWriter.cpp SHMWriter.hpp
)
target_link_libraries(
  SHMWriter
  PUBLIC
  SHMClient
)