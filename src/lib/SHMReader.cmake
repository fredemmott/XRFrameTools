include_guard(DIRECTORY)

include(SHMClient.cmake)

include(PerformanceCounters.cmake)

add_library(
  SHMReader
  STATIC
  SHMReader.cpp SHMReader.hpp
)
target_link_libraries(
  SHMReader
  PUBLIC
  SHMClient
  PRIVATE
  PerformanceCounters
)