cmake_minimum_required(VERSION 3.16)
project(endfield_poser CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 插件 DLL（仅 Windows/MSVC；沙箱 Linux 跳过）
if(WIN32)
  add_library(poser SHARED
    src/poser.cpp
    src/core/proxy_d3dcompiler.cpp
  )
  target_include_directories(poser PRIVATE
    deps deps/imgui deps/imguizmo deps/minhook/include deps/json deps/stb src)
  target_compile_definitions(poser PRIVATE
    APPLEPIE_PLUGIN_IMPL _CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN)
  target_link_libraries(poser PRIVATE d3d11 dxgi d3dcompiler dwmapi ole32)
endif()

# 数学单测（跨平台，Linux g++ 也可编）
add_executable(poser_tests
  tests/test_quat.cpp
  tests/test_ik.cpp
  tests/test_pose_file.cpp
  src/math/quat_math.h
  src/math/ik_two_bone.h
  src/math/pose_file.h
)
target_include_directories(poser_tests PRIVATE src deps/json)
enable_testing()
add_test(NAME poser_tests COMMAND poser_tests)
