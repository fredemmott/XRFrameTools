# https://github.com/microsoft/WindowsAppSDK/blob/main/docs/Coding-Guidelines/HybridCRT.md
if("${VCPKG_TARGET_TRIPLET}" MATCHES "-static$")
  set(
    CMAKE_MSVC_RUNTIME_LIBRARY
    "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  )
  add_link_options(
    "/DEFAULTLIB:ucrt$<$<CONFIG:Debug>:d>.lib" # include the dynamic UCRT
    "/NODEFAULTLIB:libucrt$<$<CONFIG:Debug>:d>.lib" # remove the static UCRT 
  )
endif()
