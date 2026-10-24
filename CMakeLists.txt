cmake_minimum_required(VERSION 3.16)
project(CustomLoadingLogo CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ---- NMDK -------------------------------------------------------------------------------------
# Build NMDK first (same ABI as this mod). Its configure step generates <NMDK_DIR>/public_include,
# the SDK headers mods compile against (virtual calls are turned into exported wrappers there,
# so these headers - not NMDK's include/ - are the right ones).
set(NMDK_DIR "${CMAKE_SOURCE_DIR}/../NMDK" CACHE PATH "NMDK source tree")
set(NMDK_PUBLIC_INCLUDE "${NMDK_DIR}/public_include" CACHE PATH "Generated NMDK SDK headers")
set(NMDK_LIBRARY "${CMAKE_SOURCE_DIR}/../NMDK/build/libnmdk.so" CACHE FILEPATH "libnmdk.so built from NMDK_DIR")
set(PRELOADER_DIR "${NMDK_DIR}/third_party/preloader")
set(PRELOADER_LIB "${PRELOADER_DIR}/libs/arm64-v8a/libpreloader.so")

if(NOT EXISTS "${NMDK_PUBLIC_INCLUDE}/nmdk/NMDK.hpp")
  message(FATAL_ERROR "NMDK SDK headers not found in '${NMDK_PUBLIC_INCLUDE}'. Configure NMDK once "
                      "(it generates public_include) or pass -DNMDK_PUBLIC_INCLUDE=...")
endif()
if(NOT EXISTS "${PRELOADER_LIB}")
  message(FATAL_ERROR "libpreloader.so not found for ABI '${ANDROID_ABI}': ${PRELOADER_LIB}")
endif()

add_library(nmdk SHARED IMPORTED)
set_target_properties(nmdk PROPERTIES IMPORTED_LOCATION "${NMDK_LIBRARY}")

add_library(preloader SHARED IMPORTED)
set_target_properties(preloader PROPERTIES
  IMPORTED_LOCATION "${PRELOADER_LIB}"
  INTERFACE_INCLUDE_DIRECTORIES "${PRELOADER_DIR}/include")

# pl/Logger.hpp needs fmt (same version NMDK uses).
# fmt 11.2.0 uses malloc() without including <cstdlib>; newer libc++ no longer provides it.
add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:SHELL:-include cstdlib>")

include(FetchContent)
FetchContent_Declare(fmt GIT_REPOSITORY https://github.com/fmtlib/fmt.git GIT_TAG 11.2.0)
set(FMT_DOC OFF CACHE BOOL "" FORCE)
set(FMT_TEST OFF CACHE BOOL "" FORCE)
set(FMT_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(fmt)

# ---- mod --------------------------------------------------------------------------------------
add_library(CustomLoadingLogo SHARED
  src/CustomLoadingLogo.cpp
)

target_include_directories(CustomLoadingLogo PRIVATE src "${NMDK_PUBLIC_INCLUDE}")
# fmt is only needed for pl/Logger.hpp declarations; header-only means no libfmt code is built.
target_link_libraries(CustomLoadingLogo PRIVATE nmdk preloader fmt::fmt-header-only log android)

include(CheckCXXCompilerFlag)
check_cxx_compiler_flag(-fdeclspec CUSTOMLOADINGLOGO_HAS_FDECLSPEC)
if(CUSTOMLOADINGLOGO_HAS_FDECLSPEC)
  target_compile_options(CustomLoadingLogo PRIVATE -fdeclspec)
endif()

target_compile_options(CustomLoadingLogo PRIVATE
  -Wno-deprecated-volatile -Wno-return-type
  -Os -flto -fvisibility=hidden -fvisibility-inlines-hidden -ffunction-sections -fdata-sections
  -fmerge-all-constants)

target_link_options(CustomLoadingLogo PRIVATE
  -Wl,-z,max-page-size=16384 -Wl,--gc-sections -Wl,--as-needed -Wl,-z,relro -Wl,-z,now
  -Wl,--exclude-libs,ALL -Wl,--icf=all -Wl,--build-id=none -Wl,--strip-all -flto)
