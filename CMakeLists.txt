cmake_minimum_required(VERSION 3.24)
project(FNVNativeStabilizer VERSION 0.4.4 LANGUAGES CXX)
if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4 OR NOT MSVC)
  message(FATAL_ERROR "Use MSVC and a Win32 build: cmake -S . -B build -A Win32")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_library(stabilizer_core STATIC
  src/runtime.cpp
  src/io_cache.cpp
  src/timing_policy.cpp
  src/device_hooks.cpp
  src/window_state.cpp
  src/audio_clock.cpp
  src/audio_transport.cpp
  src/physics.cpp
  src/audio_focus.cpp
  src/simulation_clock.cpp
)
target_include_directories(stabilizer_core PUBLIC src)
target_compile_definitions(stabilizer_core PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_compile_options(stabilizer_core PRIVATE /W4 /WX /permissive-)
target_link_libraries(stabilizer_core PUBLIC psapi)
add_library(d3d9 SHARED src/proxy.cpp src/d3d9.def)
target_link_libraries(d3d9 PRIVATE stabilizer_core)
target_compile_options(d3d9 PRIVATE /W4 /WX /permissive-)
include(CTest)
if(BUILD_TESTING)
  find_program(POWERSHELL_EXE NAMES pwsh powershell REQUIRED)
  add_executable(stabilizer_tests tests/native_tests.cpp)
  target_link_libraries(stabilizer_tests PRIVATE stabilizer_core)
  target_compile_options(stabilizer_tests PRIVATE /W4 /WX /permissive-)
  add_test(NAME native_io COMMAND stabilizer_tests)
  add_executable(presentation_policy_tests tests/presentation_policy_tests.cpp)
  target_link_libraries(presentation_policy_tests PRIVATE stabilizer_core)
  target_compile_options(presentation_policy_tests PRIVATE /W4 /WX /permissive-)
  add_test(NAME presentation_modes COMMAND presentation_policy_tests)
  add_executable(radio_transport_tests tests/radio_transport_tests.cpp)
  target_link_libraries(radio_transport_tests PRIVATE stabilizer_core)
  target_compile_options(radio_transport_tests PRIVATE /W4 /WX /permissive-)
  target_link_libraries(radio_transport_tests PRIVATE ole32 strmiids)
  add_test(NAME native_radio_transport COMMAND radio_transport_tests)
  add_executable(physics_tests tests/physics_tests.cpp)
  target_link_libraries(physics_tests PRIVATE stabilizer_core)
  target_compile_options(physics_tests PRIVATE /W4 /WX /permissive-)
  add_test(NAME high_fps_timer COMMAND physics_tests)
  add_executable(window_state_tests tests/window_state_tests.cpp)
  target_link_libraries(window_state_tests PRIVATE stabilizer_core)
  target_compile_options(window_state_tests PRIVATE /W4 /WX /permissive-)
  add_test(NAME native_window_settings COMMAND window_state_tests)
  add_executable(audio_focus_tests tests/audio_focus_tests.cpp)
  target_link_libraries(audio_focus_tests PRIVATE stabilizer_core)
  target_compile_options(audio_focus_tests PRIVATE /W4 /WX /permissive-)
  add_test(NAME native_audio_focus COMMAND audio_focus_tests)
  add_executable(simulation_clock_tests tests/simulation_clock_tests.cpp)
  target_link_libraries(simulation_clock_tests PRIVATE stabilizer_core)
  target_compile_options(simulation_clock_tests PRIVATE /W4 /WX /permissive-)
  add_test(NAME native_physics_accumulator COMMAND simulation_clock_tests)
  add_executable(audio_clock_probe tests/audio_clock_probe.cpp)
  target_link_libraries(audio_clock_probe PRIVATE stabilizer_core)
  target_compile_options(audio_clock_probe PRIVATE /W4 /WX /permissive-)
  add_executable(physics_profile_probe tests/physics_profile_probe.cpp)
  target_link_libraries(physics_profile_probe PRIVATE stabilizer_core)
  target_compile_options(physics_profile_probe PRIVATE /W4 /WX /permissive-)
  add_executable(graphics_smoke tests/graphics_smoke.cpp)
  target_compile_definitions(graphics_smoke PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
  target_compile_options(graphics_smoke PRIVATE /W4 /WX /permissive-)
  add_executable(import_host tests/import_host.cpp)
  target_compile_definitions(import_host PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
  target_compile_options(import_host PRIVATE /W4 /WX /permissive-)
  add_test(NAME graphics_native COMMAND ${POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass -File
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_graphics.ps1 -HostBinary $<TARGET_FILE:graphics_smoke> -Proxy $<TARGET_FILE:d3d9> -DX12 0)
  add_test(NAME graphics_bridge COMMAND ${POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass -File
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_graphics.ps1 -HostBinary $<TARGET_FILE:graphics_smoke> -Proxy $<TARGET_FILE:d3d9> -DX12 1)
  add_test(NAME native_focus_chain COMMAND ${POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass -File
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_graphics.ps1 -HostBinary $<TARGET_FILE:graphics_smoke> -Proxy $<TARGET_FILE:d3d9> -DX12 0 -NativeFocusHost)
  add_test(NAME native_imports COMMAND ${POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass -File
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_import_host.ps1 -HostBinary $<TARGET_FILE:import_host> -Proxy $<TARGET_FILE:d3d9> )
  add_test(NAME import_conflict COMMAND ${POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass -File
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_import_host.ps1 -HostBinary $<TARGET_FILE:import_host> -Proxy $<TARGET_FILE:d3d9> -Conflict)
endif()
