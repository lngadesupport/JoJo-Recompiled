if(NOT DEFINED JOJO_SOURCE_DIR)
  message(FATAL_ERROR "JOJO_SOURCE_DIR is required")
endif()

file(GLOB _guest_files
  "${JOJO_SOURCE_DIR}/src/core/dreamcast_*"
  "${JOJO_SOURCE_DIR}/src/core/sh4_*")
if(_guest_files)
  message(FATAL_ERROR "Dreamcast/SH-4 guest source still exists: ${_guest_files}")
endif()

foreach(_path IN ITEMS
  "src/core/game_backend.cpp" "src/core/game_backend.h"
  "src/core/native_backend.cpp" "src/core/native_backend.h"
  "src/core/native_x64.cpp" "src/core/native_x64.h")
  if(EXISTS "${JOJO_SOURCE_DIR}/${_path}")
    message(FATAL_ERROR "Old guest backend file still exists: ${_path}")
  endif()
endforeach()

file(READ "${JOJO_SOURCE_DIR}/CMakeLists.txt" _cmake)
string(TOLOWER "${_cmake}" _cmake_lower)
if(_cmake_lower MATCHES "dreamcast_|sh4_|jojo_native_backend|jojo_game_backend")
  message(FATAL_ERROR "CMake still wires old guest architecture")
endif()

foreach(_legacy_source IN ITEMS
  "src/core/conversion.cpp"
  "src/core/ps1_installation.cpp")
  string(FIND "${_cmake_lower}" "${_legacy_source}" _legacy_source_index)
  if(NOT _legacy_source_index EQUAL -1)
    message(FATAL_ERROR "Shipping jojo_core still compiles legacy install source: ${_legacy_source}")
  endif()
endforeach()

foreach(_legacy_test IN ITEMS
  "jojo_ps1_manifest_tests"
  "jojo_ps1_installation_tests"
  "jojo_ps1_conversion_tests"
  "jojo_ps1_runtime_installation_tests")
  string(FIND "${_cmake_lower}" "${_legacy_test}" _legacy_test_index)
  if(NOT _legacy_test_index EQUAL -1)
    message(FATAL_ERROR "Default CTest graph still contains legacy install test: ${_legacy_test}")
  endif()
endforeach()

file(READ "${JOJO_SOURCE_DIR}/.github/workflows/build.yml" _workflow)
string(TOLOWER "${_workflow}" _workflow_lower)
if(_workflow_lower MATCHES "usa native backend|runtime native backend|native backend manifest|jojo_game_backend|sh4")
  message(FATAL_ERROR "Workflow still runs old guest-backend contracts")
endif()

foreach(_path IN ITEMS
  "src/core/disc_image.cpp"
  "src/core/disc_media.cpp"
  "src/app_win32/main.cpp")
  file(READ "${JOJO_SOURCE_DIR}/${_path}" _text)
  string(TOLOWER "${_text}" _lower)
  if(_path STREQUAL "src/core/disc_image.cpp" AND _lower MATCHES "ext == \"gdi\"")
    message(FATAL_ERROR "disc_image still accepts GDI")
  endif()
  if(_path STREQUAL "src/core/disc_media.cpp" AND _lower MATCHES "open_gdi_source|ext == \"\\.gdi\"")
    message(FATAL_ERROR "disc_media still dispatches GDI")
  endif()
  if(_path STREQUAL "src/app_win32/main.cpp" AND _lower MATCHES "\\*\\.gdi|l\"\\.gdi\"")
    message(FATAL_ERROR "Win32 UI still offers GDI")
  endif()
endforeach()

file(READ "${JOJO_SOURCE_DIR}/src/app_win32/main.cpp" _shipping_app)
string(TOLOWER "${_shipping_app}" _shipping_app_lower)
foreach(_forbidden IN ITEMS
  "convert_image"
  "classify_installation"
  "validate_installation"
  "active_install.ini"
  "boot.psxexe"
  "generations/")
  string(FIND "${_shipping_app_lower}" "${_forbidden}" _shipping_index)
  if(NOT _shipping_index EQUAL -1)
    message(FATAL_ERROR "Shipping Win32 entry point still references legacy install token: ${_forbidden}")
  endif()
endforeach()

file(READ "${JOJO_SOURCE_DIR}/src/core/runtime.h" _runtime_header)
file(READ "${JOJO_SOURCE_DIR}/src/core/runtime.cpp" _runtime_source)
string(TOLOWER "${_runtime_header}" _runtime_header_lower)
string(TOLOWER "${_runtime_source}" _runtime_source_lower)
foreach(_forbidden IN ITEMS
  "conversion.h"
  "installationkind"
  "installationinfo"
  "classify_installation"
  "validate_installation")
  string(FIND "${_runtime_header_lower}" "${_forbidden}" _header_index)
  if(NOT _header_index EQUAL -1)
    message(FATAL_ERROR "Shipping runtime API still exposes legacy install token: ${_forbidden}")
  endif()
endforeach()
foreach(_forbidden IN ITEMS
  "active_install.ini"
  "boot.psxexe"
  "resolve_active_install_generation"
  "load_active_ps1_manifest"
  "load_validated_installed_executable")
  string(FIND "${_runtime_source_lower}" "${_forbidden}" _source_index)
  if(NOT _source_index EQUAL -1)
    message(FATAL_ERROR "Shipping runtime implementation still depends on legacy installation token: ${_forbidden}")
  endif()
endforeach()
