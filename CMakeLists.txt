cmake_minimum_required(VERSION 3.20)
project(JOJORecompiled VERSION 0.2.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()
enable_testing()

add_library(jojo_core STATIC
  src/core/version.cpp
  src/core/settings.cpp
  src/core/presentation.cpp
  src/core/input.cpp
  src/core/settings_menu.cpp
  src/core/training.cpp
  src/core/rollback.cpp
  src/core/network_protocol.cpp
  src/core/online_session.cpp
  src/core/disc_image.cpp
  src/core/disc_media.cpp
  src/core/iso9660.cpp
  src/core/ps1_system_cnf.cpp
  src/core/ps1_exe.cpp
  src/core/ps1_disc_session.cpp
  src/core/game_source_binding.cpp
  src/core/ps1_memory_bus.cpp
  src/core/ps1_hardware_services.cpp
  src/core/ps1_hardware_services.cpp
  src/core/ps1_executable_loader.cpp
  src/core/ps1_hle_bios.cpp
  src/core/ps1_boot_runtime.cpp
  src/core/ps1_boot_report_io.cpp
  src/core/ps1_commercial_frontier.cpp
  src/core/ps1_commercial_evidence.cpp
  src/core/ps1_commercial_evidence_io.cpp
  src/core/ps1_max3_explorer.cpp
  src/core/ps1_max3_report_io.cpp
  src/core/mips_decoder.cpp
  src/core/r3000a_reference_executor.cpp
  src/core/revision.cpp
  src/core/runtime.cpp
  src/core/ps1_direct_runtime.cpp
  src/core/device_id.cpp
  src/core/semver.cpp
  src/core/sha256.cpp
  src/core/mod_runtime.cpp
  src/core/mod_resolver.cpp
  src/core/mod_content.cpp
  src/core/mod_policy.cpp
  src/core/native_mod_loader.cpp
)
target_include_directories(jojo_core PUBLIC src)
if(CMAKE_DL_LIBS)
  target_link_libraries(jojo_core PUBLIC ${CMAKE_DL_LIBS})
endif()

if(WIN32)
  add_library(jojo_win32_input_host STATIC src/platform/windows/controller_win32.cpp)
  target_compile_definitions(jojo_win32_input_host PRIVATE UNICODE _UNICODE _WIN32_WINNT=0x0A00 WINVER=0x0A00)
  target_link_libraries(jojo_win32_input_host PUBLIC jojo_core setupapi hid user32)

  add_executable(jojo_recompiled WIN32 src/app_win32/main.cpp src/app_win32/presentation_host.cpp)
  set_target_properties(jojo_recompiled PROPERTIES OUTPUT_NAME "JOJO-Recompiled")
  target_compile_definitions(jojo_recompiled PRIVATE UNICODE _UNICODE _WIN32_WINNT=0x0A00 WINVER=0x0A00)
  target_link_libraries(jojo_recompiled PRIVATE jojo_core jojo_win32_input_host shell32 ole32 uuid d3d11 dxgi)

  add_executable(jojo_win32_image_selection_tests tests/test_win32_image_selection.cpp src/app_win32/main.cpp)
  target_compile_definitions(jojo_win32_image_selection_tests PRIVATE UNICODE _UNICODE _WIN32_WINNT=0x0A00 WINVER=0x0A00)
  target_link_libraries(jojo_win32_image_selection_tests PRIVATE jojo_core shell32 ole32 uuid)
  add_test(NAME jojo_win32_image_selection_tests COMMAND jojo_win32_image_selection_tests)
  set_tests_properties(jojo_win32_image_selection_tests PROPERTIES TIMEOUT 45)

  add_executable(jojo_win32_presentation_tests tests/test_win32_presentation.cpp src/app_win32/presentation_host.cpp)
  target_compile_definitions(jojo_win32_presentation_tests PRIVATE UNICODE _UNICODE _WIN32_WINNT=0x0A00 WINVER=0x0A00)
  target_link_libraries(jojo_win32_presentation_tests PRIVATE jojo_core d3d11 dxgi)
  add_test(NAME jojo_win32_presentation_tests COMMAND jojo_win32_presentation_tests)

  add_executable(jojo_win32_input_tests tests/test_win32_input.cpp)
  target_compile_definitions(jojo_win32_input_tests PRIVATE UNICODE _UNICODE _WIN32_WINNT=0x0A00 WINVER=0x0A00)
  target_link_libraries(jojo_win32_input_tests PRIVATE jojo_win32_input_host)
  add_test(NAME jojo_win32_input_tests COMMAND jojo_win32_input_tests)
endif()

function(add_jojo_test target source)
  add_executable(${target} ${source})
  target_link_libraries(${target} PRIVATE jojo_core)
  add_test(NAME ${target} COMMAND ${target})
endfunction()

add_jojo_test(jojo_tests tests/test_main.cpp)
add_jojo_test(jojo_iso_tests tests/test_iso9660.cpp)
add_jojo_test(jojo_media_tests tests/test_disc_media.cpp)
add_jojo_test(jojo_ps1_system_cnf_tests tests/test_ps1_system_cnf.cpp)
add_jojo_test(jojo_ps1_exe_tests tests/test_ps1_exe.cpp)
add_jojo_test(jojo_ps1_disc_session_tests tests/test_ps1_disc_session.cpp)
add_jojo_test(jojo_ps1_disc_streaming_tests tests/test_ps1_disc_streaming.cpp)
add_jojo_test(jojo_game_source_binding_tests tests/test_game_source_binding.cpp)
add_jojo_test(jojo_ps1_direct_runtime_tests tests/test_ps1_direct_runtime.cpp)
add_jojo_test(jojo_ps1_commercial_frontier_tests tests/test_ps1_commercial_frontier.cpp)
add_jojo_test(jojo_ps1_commercial_evidence_tests tests/test_ps1_commercial_evidence.cpp)
add_jojo_test(jojo_ps1_commercial_evidence_io_tests tests/test_ps1_commercial_evidence_io.cpp)
add_jojo_test(jojo_ps1_hardware_irq_timer_tests tests/test_ps1_hardware_irq_timers.cpp)
add_jojo_test(jojo_ps1_memory_bus_tests tests/test_ps1_memory_bus.cpp)
add_jojo_test(jojo_ps1_executable_loader_tests tests/test_ps1_executable_loader.cpp)
add_jojo_test(jojo_ps1_boot_runtime_tests tests/test_ps1_boot_runtime.cpp)
add_jojo_test(jojo_ps1_hle_bios_tests tests/test_ps1_hle_bios.cpp)
add_jojo_test(jojo_ps1_changeclearrcnt_tests tests/test_ps1_changeclearrcnt.cpp)
add_jojo_test(jojo_ps1_96_remove_tests tests/test_ps1_96_remove.cpp)
add_jojo_test(jojo_ps1_diagnostic_frontier_tests tests/test_ps1_diagnostic_frontier.cpp)
add_jojo_test(jojo_ps1_max3_explorer_tests tests/test_ps1_max3_explorer.cpp)
add_jojo_test(jojo_ps1_boot_report_io_tests tests/test_ps1_boot_report_io.cpp)
add_jojo_test(jojo_mips_decoder_tests tests/test_mips_decoder.cpp)
add_jojo_test(jojo_r3000a_integer_tests tests/test_r3000a_integer.cpp)
add_jojo_test(jojo_r3000a_exception_tests tests/test_r3000a_exceptions.cpp)
add_jojo_test(jojo_r3000a_hilo_tests tests/test_r3000a_hilo.cpp)
add_jojo_test(jojo_r3000a_control_flow_tests tests/test_r3000a_control_flow.cpp)
add_jojo_test(jojo_r3000a_memory_tests tests/test_r3000a_memory.cpp)
add_jojo_test(jojo_r3000a_unaligned_tests tests/test_r3000a_unaligned.cpp)
add_jojo_test(jojo_r3000a_cop0_tests tests/test_r3000a_cop0.cpp)
add_jojo_test(jojo_r3000a_boundary_tests tests/test_r3000a_boundary.cpp)
add_jojo_test(jojo_r3000a_init_tests tests/test_r3000a_init.cpp)
add_jojo_test(jojo_presentation_tests tests/test_presentation.cpp)
add_jojo_test(jojo_semver_tests tests/test_semver.cpp)
add_jojo_test(jojo_sha256_tests tests/test_sha256.cpp)
add_jojo_test(jojo_mod_runtime_tests tests/test_mod_runtime.cpp)
add_jojo_test(jojo_m5_settings_input_tests tests/test_m5_settings_input.cpp)
add_jojo_test(jojo_training_tests tests/test_training.cpp)
add_jojo_test(jojo_rollback_tests tests/test_rollback.cpp)
add_jojo_test(jojo_online_session_tests tests/test_online_session.cpp)
add_jojo_test(jojo_mod_policy_tests tests/test_mod_policy.cpp)

function(add_jojo_native_mod_fixture target mod_id)
  add_library(${target} SHARED tests/fixtures/test_native_mod.cpp)
  target_include_directories(${target} PRIVATE src)
  target_compile_definitions(${target} PRIVATE JOJO_TEST_NATIVE_MOD_ID=\"${mod_id}\")
endfunction()

add_jojo_native_mod_fixture(jojo_native_mod_fixture_first native.first)
add_jojo_native_mod_fixture(jojo_native_mod_fixture_second native.second)
add_jojo_native_mod_fixture(jojo_native_mod_fixture_failure native.failure)
target_compile_definitions(jojo_native_mod_fixture_failure PRIVATE JOJO_TEST_NATIVE_MOD_LOAD_RESULT=7)
add_jojo_native_mod_fixture(jojo_native_mod_fixture_bad_abi native.badabi)
target_compile_definitions(jojo_native_mod_fixture_bad_abi PRIVATE JOJO_TEST_NATIVE_MOD_ABI=2)
add_jojo_native_mod_fixture(jojo_native_mod_fixture_no_export native.noexport)
target_compile_definitions(jojo_native_mod_fixture_no_export PRIVATE JOJO_TEST_NATIVE_MOD_NO_EXPORT=1)

add_executable(jojo_native_mod_loader_tests tests/test_native_mod_loader.cpp)
target_link_libraries(jojo_native_mod_loader_tests PRIVATE jojo_core)
add_dependencies(jojo_native_mod_loader_tests
  jojo_native_mod_fixture_first
  jojo_native_mod_fixture_second
  jojo_native_mod_fixture_failure
  jojo_native_mod_fixture_bad_abi
  jojo_native_mod_fixture_no_export)
add_test(NAME jojo_native_mod_loader_tests COMMAND jojo_native_mod_loader_tests
  "$<TARGET_FILE:jojo_native_mod_fixture_first>"
  "$<TARGET_FILE:jojo_native_mod_fixture_second>"
  "$<TARGET_FILE:jojo_native_mod_fixture_failure>"
  "$<TARGET_FILE:jojo_native_mod_fixture_bad_abi>"
  "$<TARGET_FILE:jojo_native_mod_fixture_no_export>")