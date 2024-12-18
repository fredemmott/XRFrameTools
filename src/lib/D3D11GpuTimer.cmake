include_guard(GLOBAL)

add_library(
  D3D11GpuTimer
  STATIC
  EXCLUDE_FROM_ALL
  D3D11GpuTimer.cpp D3D11GpuTimer.hpp
)
target_link_libraries(
  D3D11GpuTimer
  PUBLIC
  WIL::WIL
  d3d11
)