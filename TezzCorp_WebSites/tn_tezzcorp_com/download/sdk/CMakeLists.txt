cmake_minimum_required(VERSION 3.20)

project(TezzNative C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin")

file(GLOB TEZZ_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")

add_executable(tezzc ${TEZZ_SOURCES})
target_include_directories(tezzc PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_compile_definitions(tezzc PRIVATE TN_ENABLE_HOST_GUI=1)

if(MSVC)
  target_compile_definitions(tezzc PRIVATE _CRT_SECURE_NO_WARNINGS)
  # /O2 = speed, /Oi = intrinsics, /Ot = fast, /GL = whole-program opt
  target_compile_options(tezzc PRIVATE /W4 /O2 /Oi /Ot /GL /GS- /fp:fast)
  target_link_options(tezzc PRIVATE /LTCG /OPT:REF /OPT:ICF)
else()
  target_compile_options(tezzc PRIVATE -O3 -Wall -Wextra -Wno-unused-parameter
    -Wno-unused-function -march=native -funroll-loops -fomit-frame-pointer)
  # LTO only on non-MinGW (MinGW ar requires -fuse-linker-plugin)
  if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT MINGW)
    target_compile_options(tezzc PRIVATE -flto)
    target_link_options(tezzc PRIVATE -flto)
  endif()
  if(MINGW)
    # MinGW-specific: enable LTO-safe optimizations
    target_compile_options(tezzc PRIVATE -ffunction-sections -fdata-sections)
    target_link_options(tezzc PRIVATE -Wl,--gc-sections)
  endif()
endif()

if(WIN32)
  target_link_libraries(tezzc PRIVATE ws2_32 secur32 crypt32 winhttp user32 gdi32)
  add_custom_command(TARGET tezzc POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:tezzc> "${CMAKE_CURRENT_SOURCE_DIR}/bin/tezzc-windows-x64.exe"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:tezzc> "${CMAKE_CURRENT_SOURCE_DIR}/bin/tezzc.exe")
else()
  find_package(OpenSSL REQUIRED)
  find_package(Threads REQUIRED)
  target_link_libraries(tezzc PRIVATE OpenSSL::SSL OpenSSL::Crypto Threads::Threads m)
  add_custom_command(TARGET tezzc POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:tezzc> "${CMAKE_CURRENT_SOURCE_DIR}/bin/tezzc-linux-x64")
endif()
