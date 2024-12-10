include_guard(DIRECTORY)

include(PerformanceCounters.cmake)

add_library(
  FrameMetrics
  STATIC
  FrameMetrics.cpp FrameMetrics.hpp
  MetricsAggregator.cpp MetricsAggregator.hpp
)
target_link_libraries(
  FrameMetrics
  PRIVATE
  PerformanceCounters
)