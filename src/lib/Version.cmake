include_guard(GLOBAL)

block()
  SET(VERSION_HPP "${CMAKE_CURRENT_BINARY_DIR}/include/Version.hpp")
  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/version.hpp.in"
    "${VERSION_HPP}"
    @ONLY
    NEWLINE_STYLE UNIX
  )
  add_library(Version INTERFACE)
  target_include_directories(Version INTERFACE "${CMAKE_CURRENT_BINARY_DIR}/include")
endblock()