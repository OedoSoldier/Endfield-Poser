cmake_minimum_required(VERSION 3.16)
project(endfield_poser CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MSVC：源码含 UTF-8 中文注释/字符串，缺 /utf-8 时 GBK 误读会把
# 行尾字节解析成反斜杠续行符，导致下一行被吞（C2046 等假错误）。
if(MSVC)
  add_compile_options(/utf-8)
  # The game ships MSVCP140 14.29; newer constexpr mutex initialization is
  # incompatible with that private runtime. Keep the legacy initialization ABI.
  add_compile_definitions(_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR)
endif()

# 输出目录统一到 plugin/（与 build.bat 的部署布局一致）
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ---- 插件 DLL + 代理 DLL（仅 Windows/MSVC；沙箱 Linux 跳过）----
if(WIN32)
  # 主插件 DLL
  add_library(poser SHARED
    src/poser.cpp
    src/poser.rc
    src/config.h
    src/math/mmd_motion.h src/math/mmd_rig.h src/math/mmd_retarget.h src/math/mmd_props.h src/math/mmd_adaptation.h src/math/mmd_amplitude.h
    src/game/mmd_player.h src/game/mmd_io.h src/game/frozen_grip.h src/editor/panel_mmd.h src/editor/panel_mmd_adaptation.h
    src/core/game_hooks.h
    src/core/gui_overlay.h
    src/core/frame_driver.h src/math/frame_cadence.h
    src/game/mmd_audio.h src/math/mmd_audio_sync.h
    src/game/skeleton.h src/game/freeze.h src/game/accessory.h src/game/morph.h src/game/smc_morph.h
    src/game/char_state.h
    src/editor/selection.h src/editor/panel_library.h src/editor/panel_morph.h
    src/editor/panel_bones.h
    src/editor/ik_control.h
    deps/imgui/imgui.cpp
    deps/imgui/imgui_draw.cpp
    deps/imgui/imgui_tables.cpp
    deps/imgui/imgui_widgets.cpp
    deps/imgui/imgui_impl_dx11.cpp
    deps/imgui/imgui_impl_win32.cpp
    deps/imguizmo/ImGuizmo.cpp
  )
  target_include_directories(poser PRIVATE
    deps deps/imgui deps/imguizmo deps/minhook_lib/include deps/json src)
  target_compile_definitions(poser PRIVATE
    APPLEPIE_PLUGIN_IMPL _CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN
    IMGUI_DEFINE_MATH_OPERATORS)
  # /EHa：让 __except 也能捕获 C++ 异常（IL2CPP 调用可能抛 0xe06d7363，
  # /EHsc 下这类异常会直接崩掉游戏进程）
  target_compile_options(poser PRIVATE /EHa)
  target_link_libraries(poser PRIVATE
    d3d11 dxgi d3dcompiler dwmapi ole32
    ${CMAKE_CURRENT_SOURCE_DIR}/deps/minhook_lib/lib/libMinHook.x64.lib)

  # 代理 DLL：本地编译 src/core/proxy_d3dcompiler.cpp，不再依赖 EIEM 编译产物
  add_library(d3dcompiler_47 SHARED src/core/proxy_d3dcompiler.cpp)
  target_compile_definitions(d3dcompiler_47 PRIVATE _CRT_SECURE_NO_WARNINGS)
  set_target_properties(d3dcompiler_47 PROPERTIES OUTPUT_NAME "d3dcompiler_47")

  # Vulkan 代理 DLL（摘自 EIEM，转发 System32\vulkan-1）
  add_library(vulkan-1 SHARED src/core/proxy_vulkan_full.cpp)
  target_compile_definitions(vulkan-1 PRIVATE _CRT_SECURE_NO_WARNINGS)
  set_target_properties(vulkan-1 PROPERTIES OUTPUT_NAME "vulkan-1")

  add_dependencies(poser d3dcompiler_47 vulkan-1)
  file(GLOB MMD_RIG_PRESETS "${CMAKE_CURRENT_SOURCE_DIR}/presets/mmd/*.mmdrig.json")
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/plugin/mmd/rig-presets")
  foreach(PRESET ${MMD_RIG_PRESETS})
    get_filename_component(PRESET_NAME "${PRESET}" NAME)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/plugin/mmd/rig-presets/${PRESET_NAME}")
      configure_file("${PRESET}" "${CMAKE_CURRENT_SOURCE_DIR}/plugin/mmd/rig-presets/${PRESET_NAME}" COPYONLY)
    endif()
  endforeach()

  # 部署：把 poser.dll 与 d3dcompiler_47.dll 拷到 plugin/
  add_custom_command(TARGET poser POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_SOURCE_DIR}/plugin
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:poser>
            ${CMAKE_CURRENT_SOURCE_DIR}/plugin/poser.dll
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:d3dcompiler_47>
            ${CMAKE_CURRENT_SOURCE_DIR}/plugin/d3dcompiler_47.dll
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:vulkan-1>
            ${CMAKE_CURRENT_SOURCE_DIR}/plugin/vulkan-1.dll)
endif()

# Private local tests are optional; public checkouts build without them.
option(POSER_BUILD_TESTS "Build private local tests" OFF)
if(POSER_BUILD_TESTS)
enable_testing()

# ---- 数学单测（跨平台，Linux g++ 也可编；每个测试独立可执行）----
function(add_poser_test name src)
  if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${src}")
    message(FATAL_ERROR "POSER_BUILD_TESTS requires local test sources: ${src}")
  endif()
  add_executable(${name} ${src} src/math/quat_math.h src/math/ik_two_bone.h src/math/pose_file.h)
  target_include_directories(${name} PRIVATE src deps/json)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

add_poser_test(test_quat tests/test_quat.cpp)
add_poser_test(test_ik tests/test_ik.cpp)
add_poser_test(test_pose_file tests/test_pose_file.cpp)
add_poser_test(test_mmd tests/test_mmd.cpp)
add_poser_test(test_mmd_transport tests/test_mmd_transport.cpp)
if(MSVC)
  add_poser_test(test_mmd_audio tests/test_mmd_audio.cpp)
  target_compile_options(test_mmd_audio PRIVATE /EHa)
  add_poser_test(test_frame_driver tests/test_frame_driver.cpp)
  target_include_directories(test_frame_driver PRIVATE deps/minhook_lib/include)
  target_compile_options(test_frame_driver PRIVATE /EHa)
  target_link_libraries(test_frame_driver PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/deps/minhook_lib/lib/libMinHook.x64.lib)
  add_poser_test(test_mmd_runtime tests/test_mmd_runtime.cpp)
  target_compile_options(test_mmd_runtime PRIVATE /EHa)
  add_poser_test(test_character_capture tests/test_character_capture.cpp)
  target_include_directories(test_character_capture PRIVATE deps/minhook_lib/include)
  target_compile_options(test_character_capture PRIVATE /EHa)
  target_link_libraries(test_character_capture PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/deps/minhook_lib/lib/libMinHook.x64.lib)
  add_poser_test(test_smc_switch tests/test_smc_switch.cpp)
  target_include_directories(test_smc_switch PRIVATE deps/minhook_lib/include)
  target_compile_options(test_smc_switch PRIVATE /EHa)
  target_link_libraries(test_smc_switch PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/deps/minhook_lib/lib/libMinHook.x64.lib)
endif()

endif()
