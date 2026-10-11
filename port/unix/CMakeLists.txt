cmake_minimum_required(VERSION 3.16)
project(earthbound C)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_C_STANDARD 11)

# Emit compile_commands.json so clangd/IDEs resolve include paths and types.
# A root-level symlink (../../compile_commands.json) points at this file.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(EB_VIEWPORT_WIDTH 400 CACHE STRING "Viewport width in pixels")
set(EB_VIEWPORT_HEIGHT 224 CACHE STRING "Viewport height in pixels")

# Build game library from src/
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../src
                 ${CMAKE_CURRENT_BINARY_DIR}/game_lib)

# Find SDL2
find_package(SDL2 QUIET)
if(NOT SDL2_FOUND)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED sdl2)
endif()

# Platform executable. Use GLOB_RECURSE (not plain GLOB): a non-recursive
# `file(GLOB ... CONFIGURE_DEPENDS ...)` silently returns empty on some
# filesystems (notably the Docker Desktop virtiofs mount used by the i386 build
# in port/unix/docker), which drops every platform/*.c object and breaks the
# link. GLOB_RECURSE + CONFIGURE_DEPENDS works there and matches src/CMakeLists.txt.
# platform/ has no subdirectories, so this is equivalent to a flat glob.
file(GLOB_RECURSE PLATFORM_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/platform/*.c")

# Vulkan presenter (platform/vk_renderer.c). Needs the Vulkan headers and loader; without them the
# build still works and the game falls back to the SDL renderer.
option(EB_ENABLE_VULKAN "Build the Vulkan renderer" ON)
set(EB_VULKAN_FOUND OFF)
if(EB_ENABLE_VULKAN)
    find_path(EB_VULKAN_INCLUDE vulkan/vulkan.h)
    find_library(EB_VULKAN_LIB NAMES vulkan libvulkan.so.1 vulkan-1)
    if(EB_VULKAN_INCLUDE AND EB_VULKAN_LIB)
        set(EB_VULKAN_FOUND ON)
    else()
        message(WARNING "Vulkan headers/loader not found: building without the Vulkan renderer (SDL renderer only)")
    endif()
endif()
if(NOT EB_VULKAN_FOUND)
    list(FILTER PLATFORM_SOURCES EXCLUDE REGEX "vk_renderer\\.c$")
endif()

add_executable(earthbound main.c ${PLATFORM_SOURCES})
target_include_directories(earthbound PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(EB_VULKAN_FOUND)
    target_compile_definitions(earthbound PRIVATE EB_HAVE_VULKAN=1)
    target_include_directories(earthbound PRIVATE ${EB_VULKAN_INCLUDE})
    target_link_libraries(earthbound ${EB_VULKAN_LIB})
endif()

target_link_libraries(earthbound earthbound_game m)

if(TARGET SDL2::SDL2)
    target_link_libraries(earthbound SDL2::SDL2)
else()
    target_include_directories(earthbound PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(earthbound ${SDL2_LIBRARIES})
    target_link_directories(earthbound PRIVATE ${SDL2_LIBRARY_DIRS})
endif()

# Platform code needs SDL2 headers and game library headers (inherited via PUBLIC)
target_include_directories(earthbound PRIVATE
    ${SDL2_INCLUDE_DIRS}
)

if(MSVC)
    target_compile_options(earthbound PRIVATE /W4)
else()
    target_compile_options(earthbound PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# Sanitizer support
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)

set(_SANITIZERS "")
if(ENABLE_ASAN)
    list(APPEND _SANITIZERS "address")
endif()
if(ENABLE_UBSAN)
    list(APPEND _SANITIZERS "undefined")
endif()

if(_SANITIZERS)
    list(JOIN _SANITIZERS "," _SANITIZER_FLAGS)
    target_compile_options(earthbound PRIVATE -fsanitize=${_SANITIZER_FLAGS} -fno-omit-frame-pointer)
    target_link_options(earthbound PRIVATE -fsanitize=${_SANITIZER_FLAGS})
endif()

# Automated checks (no window, GPU, speakers or controller needed): `ctest --test-dir build --output-on-failure`
enable_testing()
add_test(NAME savestate_and_save COMMAND earthbound --selftest-savestate --save ${CMAKE_CURRENT_BINARY_DIR}/ctest.srm)
add_test(NAME gamepad_virtual COMMAND earthbound --selftest-pad)
set_tests_properties(gamepad_virtual PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
add_test(NAME cli_help COMMAND earthbound --help)
add_test(NAME asset_override_rules COMMAND earthbound --selftest-mods)

# Unit tests for the pure (SDL-free) helpers: viewport/layout maths.
option(EB_BUILD_TESTS "Build the unit tests" ON)
if(EB_BUILD_TESTS)
    add_executable(test_video_layout tests/test_video_layout.c platform/video_layout.c)
    target_include_directories(test_video_layout PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
    add_test(NAME video_layout COMMAND test_video_layout)
endif()

# Native S-DSP vs lakesnes DSP on real music (bit-exact), needs the embedded assets only.
add_test(NAME config_handling COMMAND earthbound --selftest-config)
add_test(NAME hq_music_validation COMMAND earthbound --selftest-hqmusic)
add_test(NAME audio_native_dsp COMMAND earthbound --selftest-audio 1 8 360)
