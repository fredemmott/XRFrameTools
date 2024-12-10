include_guard(DIRECTORY)

add_library(
  Win32Utils
  STATIC
  Win32Utils.cpp Win32Utils.hpp
)
target_link_libraries(
  Win32Utils
  PRIVATE
  WIL::WIL
)