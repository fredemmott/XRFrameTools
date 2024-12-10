include_guard(DIRECTORY)

add_library(
  SHMClient
  STATIC
  "${ABI_KEY_HPP}"
  SHM.hpp
  SHMClient.cpp SHMClient.hpp
)
target_link_libraries(
  SHMClient
  PUBLIC
  WIL::WIL
)
target_include_directories(
  SHMClient
  PRIVATE
  "${GENERATED_INCLUDE_DIR}"
)
