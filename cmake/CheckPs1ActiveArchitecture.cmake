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
