cmake_minimum_required(VERSION 3.22)

project(bitematica LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------------
# Sources that do not touch the preloader SDK or Android.
#
# Keeping the format, mesh and UI code in a target that builds without Android is what
# lets `bitematica_tests` run on a host, so the parts most likely to be subtly wrong
# (NBT decoding, palette unpacking, the projector) are covered by a real test suite.
# ---------------------------------------------------------------------------------
set(BITEMATICA_CORE_SOURCES
    src/nbt/NbtReader.cpp
    src/nbt/NbtWriter.cpp
    src/format/SchematicFormat.cpp
    src/format/LitematicReader.cpp
    src/format/SpongeReader.cpp
    src/format/McEditReader.cpp
    src/format/BedrockStructureReader.cpp
    src/format/StructureWriter.cpp
    src/render/HudOverlay.cpp
    src/mod/ControllerNavigator.cpp
    src/mod/JsonConfig.cpp)

add_library(bitematica_core STATIC ${BITEMATICA_CORE_SOURCES})
target_include_directories(bitematica_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

# zlib: the NDK's sysroot ships it, so Android links `z` directly; a host build uses
# the system package. Both only need the classic inflate/deflate entry points.
if(ANDROID)
  find_library(BITEMATICA_ZLIB z)
  if(NOT BITEMATICA_ZLIB)
    message(FATAL_ERROR "zlib (libz) was not found in the NDK sysroot")
  endif()
  target_link_libraries(bitematica_core PUBLIC "${BITEMATICA_ZLIB}")
else()
  find_package(ZLIB REQUIRED)
  target_link_libraries(bitematica_core PUBLIC ZLIB::ZLIB)
endif()

# -----------------------------------------------
# Android native mod (.AntEgg `type: native`)
# -----------------------------------------------
if(ANDROID)
  # The preloader SDK is provided either as a local checkout (the GlowberryClient
  # submodule path) or an external root passed by the build script.
  set(PRELOADER_ANDROID_ROOT
      "${CMAKE_CURRENT_SOURCE_DIR}/third_party/preloader-android"
      CACHE PATH "Path to a ChimeraAnt-DEV/preloader-android checkout")

  if(NOT EXISTS "${PRELOADER_ANDROID_ROOT}/include/pl/Mod.hpp")
    message(FATAL_ERROR
        "PRELOADER_ANDROID_ROOT must point at a preloader-android checkout. "
        "Missing: ${PRELOADER_ANDROID_ROOT}/include/pl/Mod.hpp")
  endif()

  if(ANDROID_ABI)
    set(BITEMATICA_ABI "${ANDROID_ABI}")
  else()
    set(BITEMATICA_ABI "arm64-v8a")
  endif()
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY
      "${CMAKE_BINARY_DIR}/out/${BITEMATICA_ABI}")

  find_library(ANDROID_LOG_LIB log)

  # The preloader's public headers (pl/Logger.hpp in particular) use fmt, so the mod
  # needs fmt headers even though it links none of the preloader's code: the SDK symbols
  # are resolved by the loader at runtime. Fetch fmt the same way the preloader does.
  if(NOT TARGET fmt::fmt)
    include(FetchContent)
    FetchContent_Declare(
        fmt
        GIT_REPOSITORY https://github.com/fmtlib/fmt.git
        GIT_TAG 11.2.0)
    set(FMT_DOC OFF CACHE BOOL "Disable fmt docs" FORCE)
    set(FMT_TEST OFF CACHE BOOL "Disable fmt tests" FORCE)
    set(FMT_INSTALL OFF CACHE BOOL "Disable fmt install rules" FORCE)
    FetchContent_MakeAvailable(fmt)
  endif()

  add_library(bitematica SHARED src/mod/BiteMaticaMod.cpp)
  target_include_directories(bitematica PRIVATE "${PRELOADER_ANDROID_ROOT}/include")
  target_link_libraries(bitematica PRIVATE bitematica_core fmt::fmt ${ANDROID_LOG_LIB})
  target_compile_options(bitematica PRIVATE
      $<$<COMPILE_LANG_AND_ID:CXX,Clang,GNU>:
        -Wall -Wextra -Wpedantic -fvisibility=hidden -fvisibility-inlines-hidden>)
  set_target_properties(bitematica PROPERTIES OUTPUT_NAME "bitematica")

  # The SDK symbols (`pl::mod`, `pl::modmenu`, `pl::input`) come from the preloader.
  # Build it as a subproject and link it: the mod's NEEDED entry then names
  # libpreloader.so, which the loader has already mapped when it injects the mod, so
  # the symbols bind to the running preloader. This is the same path the SDK's own
  # full-cpp-mod example uses. `--unresolved-symbols=ignore-all` is only a fallback for
  # the (unsupported) case of building the mod without a preloader checkout at all.
  option(BITEMATICA_LINK_PRELOADER
         "Build and link the local preloader-android target" ON)
  if(BITEMATICA_LINK_PRELOADER AND NOT TARGET preloader)
    add_subdirectory("${PRELOADER_ANDROID_ROOT}"
                     "${CMAKE_BINARY_DIR}/_preloader_android")
  endif()
  if(TARGET preloader)
    target_link_libraries(bitematica PRIVATE preloader)
  else()
    target_link_options(bitematica PRIVATE -Wl,--unresolved-symbols=ignore-all)
  endif()
endif()

# -------------------------------
# Host test target
# -------------------------------
if(NOT ANDROID)
  enable_testing()
  add_executable(bitematica_tests test/host_tests.cpp)
  target_link_libraries(bitematica_tests PRIVATE bitematica_core)
  target_compile_definitions(bitematica_tests PRIVATE
      BITEMATICA_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures")
  add_test(NAME bitematica_tests COMMAND bitematica_tests)
endif()
