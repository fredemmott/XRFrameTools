include_guard(DIRECTORY)

add_library(
  Config
  STATIC
  Config.cpp Config.hpp
)
target_link_libraries(
  Config
  PUBLIC
  WIL::WIL
)