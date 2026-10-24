cmake_minimum_required(VERSION 3.21)
project(hyper_extract VERSION 0.7.0 LANGUAGES C CXX)
if(WIN32 AND DEFINED ENV{HX_ZIG} AND CMAKE_CXX_COMPILER MATCHES "zig_cxx")
  set(CMAKE_AR "${CMAKE_CURRENT_SOURCE_DIR}/tools/zig_ar.cmd")
  set(CMAKE_RANLIB "${CMAKE_CURRENT_SOURCE_DIR}/tools/zig_ranlib.cmd")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(WIN32)
  add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

if(MSVC)
  add_compile_options(/W4 /utf-8 /permissive- /EHsc)
  add_compile_definitions(UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
  # static CRT: single exe, no VC redist needed on the target PC
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
  add_compile_options(-Wall -Wextra)
endif()

# Repository checkout stores full upstream sources in one verified archive.
# Full delivery ZIP already contains the expanded directories.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/vendor-sources.zip")
  if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libdeflate126/CMakeLists.txt"
     AND NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib132/CMakeLists.txt"
     AND NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/7zip/CPP/7zip/Archive/IArchive.h")
    file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/third_party/vendor-sources.zip" HX_VENDOR_HASH)
    if(NOT HX_VENDOR_HASH STREQUAL "3bc2a51c85d93f10f88535156fda1d91312ad991a4fd016e6a23c20c68386d23")
      message(FATAL_ERROR "Bundled vendor source integrity mismatch")
    endif()
    file(ARCHIVE_EXTRACT INPUT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/vendor-sources.zip"
         DESTINATION "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
  endif()
endif()

# ---- portable core (PathGuard, DirPlanner, stats, CRC32) ----
add_library(hx_core STATIC
  src/core/crc32.cpp
  src/core/dirplan.cpp
  src/core/pathguard.cpp
  src/core/stats.cpp)
target_include_directories(hx_core PUBLIC src)

# ---- portable unit tests ----
add_executable(hx_tests tests/test_core.cpp)
target_link_libraries(hx_tests PRIVATE hx_core)
add_executable(hx_pathguard_prop tests/test_pathguard_prop.cpp)
target_link_libraries(hx_pathguard_prop PRIVATE hx_core)
enable_testing()
add_test(NAME core COMMAND hx_tests)
add_test(NAME pathguard_prop COMMAND hx_pathguard_prop 2000)

# ---- Windows: OutputSink + AutoTune + hx-tune.exe ----
if(WIN32)
  add_library(hx_output STATIC src/core/output/win32_sink.cpp)
  target_link_libraries(hx_output PUBLIC hx_core)

  add_library(hx_autotune STATIC
    src/autotune/dataset.cpp
    src/autotune/env.cpp
    src/autotune/trial.cpp
    src/autotune/tuner.cpp)
  target_link_libraries(hx_autotune PUBLIC hx_output)

  add_executable(hx-tune src/tools/hx_tune_main.cpp src/tools/selftest_win.cpp)
  target_link_libraries(hx-tune PRIVATE hx_autotune shell32 advapi32)
endif()

# ---- ZIP libraries: vendored source first, system libraries on Linux, pinned FetchContent fallback.
# On Windows the one-click bootstrap vendors both libraries, enabling offline rebuilds.
include(FetchContent)
option(HX_PREFER_SYSTEM_LIBS "Use OS-installed libdeflate/zlib if vendored sources unavailable" ON)
option(HX_ALLOW_FETCH "Allow CMake to download missing third party sources" OFF)
set(LIBDEFLATE_BUILD_SHARED_LIB OFF CACHE BOOL "" FORCE)
set(LIBDEFLATE_BUILD_GZIP OFF CACHE BOOL "" FORCE)
set(LIBDEFLATE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libdeflate126/CMakeLists.txt")
  add_subdirectory(third_party/libdeflate126 EXCLUDE_FROM_ALL)
  add_library(hx_libdeflate INTERFACE)
  target_link_libraries(hx_libdeflate INTERFACE libdeflate_static)
elseif(HX_PREFER_SYSTEM_LIBS)
  find_path(HX_LIBDEFLATE_INCLUDE libdeflate.h)
  find_library(HX_LIBDEFLATE_LIBRARY NAMES deflate libdeflate)
  if(HX_LIBDEFLATE_INCLUDE AND HX_LIBDEFLATE_LIBRARY)
    add_library(hx_libdeflate UNKNOWN IMPORTED)
    set_target_properties(hx_libdeflate PROPERTIES
      IMPORTED_LOCATION "${HX_LIBDEFLATE_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${HX_LIBDEFLATE_INCLUDE}")
  endif()
endif()
if(NOT TARGET hx_libdeflate)
  if(NOT HX_ALLOW_FETCH)
    message(FATAL_ERROR "libdeflate missing; vendor third_party/libdeflate or set HX_ALLOW_FETCH=ON")
  endif()
  FetchContent_Declare(libdeflate URL https://github.com/ebiggers/libdeflate/releases/download/v1.26/libdeflate-1.26.tar.gz
    URL_HASH SHA256=125856d4656e0feab660f94842f835923410c9281fedbcee64598c918da42b5a
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(libdeflate)
  add_library(hx_libdeflate INTERFACE)
  target_link_libraries(hx_libdeflate INTERFACE libdeflate_static)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib132/CMakeLists.txt")
  set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
  set(ZLIB_BUILD_SHARED OFF CACHE BOOL "" FORCE)
  set(ZLIB_BUILD_STATIC ON CACHE BOOL "" FORCE)
  add_subdirectory(third_party/zlib132 EXCLUDE_FROM_ALL)
  if(NOT TARGET ZLIB::ZLIB)
    add_library(ZLIB::ZLIB ALIAS zlibstatic)
  endif()
elseif(HX_PREFER_SYSTEM_LIBS)
  find_package(ZLIB QUIET)
endif()
if(NOT TARGET ZLIB::ZLIB)
  if(NOT HX_ALLOW_FETCH)
    message(FATAL_ERROR "zlib missing; vendor third_party/zlib or set HX_ALLOW_FETCH=ON")
  endif()
  FetchContent_Declare(zlib URL https://zlib.net/zlib132.zip
    URL_HASH SHA256=e8bf55f3017aa181690990cb58a994e77885da140609fc8f94abe9b65d2cae28
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
  set(ZLIB_BUILD_SHARED OFF CACHE BOOL "" FORCE)
  set(ZLIB_BUILD_STATIC ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(zlib)
  add_library(ZLIB::ZLIB ALIAS zlibstatic)
endif()

add_library(hx_zip STATIC
  src/core/zip/zip_archive.cpp
  src/core/zip/zip_decoder.cpp
  src/core/zip/zip_writer.cpp)
target_link_libraries(hx_zip PUBLIC hx_core hx_libdeflate ZLIB::ZLIB)
target_include_directories(hx_zip PUBLIC src)

add_executable(hx_zip_tests tests/test_zip.cpp)
target_link_libraries(hx_zip_tests PRIVATE hx_zip)
add_test(NAME zip COMMAND hx_zip_tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures)

if(WIN32)
  add_library(hx_zip_win STATIC src/core/zip/zip_extract_win.cpp)
  target_link_libraries(hx_zip_win PUBLIC hx_zip hx_output)
  add_library(hx_service STATIC src/core/archive_service.cpp src/core/sdk_archive.cpp src/core/profile.cpp src/core/zip/zip_tune_win.cpp src/tools/selftest_win.cpp src/tools/archive_selftest.cpp)
  target_link_libraries(hx_service PUBLIC hx_zip_win hx_autotune shell32 advapi32 ole32 oleaut32 uuid psapi)
  add_executable(hx src/tools/hx_zip_main.cpp)
  target_link_libraries(hx PRIVATE hx_service)
  add_executable(HyperExtractGUI WIN32 src/tools/gui_win.cpp)
  target_link_libraries(HyperExtractGUI PRIVATE hx_service comdlg32 comctl32 gdi32)
  if(NOT MSVC)
    target_link_options(HyperExtractGUI PRIVATE -municode)
  endif()
  if(DEFINED ENV{HX_ZIG} AND CMAKE_CXX_COMPILER MATCHES "zig_cxx")
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/version.res"
      COMMAND "$ENV{HX_ZIG}" rc /fo "${CMAKE_CURRENT_BINARY_DIR}/version.res" version.rc
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/src/tools"
      DEPENDS src/tools/version.rc src/tools/app.manifest VERBATIM)
    target_sources(hx PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/version.res")
    target_sources(HyperExtractGUI PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/version.res")
  elseif(MSVC)
    enable_language(RC)
    target_sources(hx PRIVATE src/tools/version.rc)
    target_sources(HyperExtractGUI PRIVATE src/tools/version.rc)
  endif()
endif()

add_executable(hx_zip_mutation tests/test_zip_mutation.cpp)
target_link_libraries(hx_zip_mutation PRIVATE hx_zip)
add_test(NAME zip_mutation COMMAND hx_zip_mutation ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures)

add_executable(hx_zip_writer_tests tests/test_zip_writer.cpp)
target_link_libraries(hx_zip_writer_tests PRIVATE hx_zip)
add_test(NAME zip_writer COMMAND hx_zip_writer_tests)
add_executable(hx_zip_security_tests tests/test_zip_security.cpp)
target_link_libraries(hx_zip_security_tests PRIVATE hx_zip)
add_test(NAME zip_security COMMAND hx_zip_security_tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures)
if(WIN32)
  add_test(NAME windows_selftest COMMAND hx selftest)
  set_tests_properties(windows_selftest PROPERTIES ENVIRONMENT "HX_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/selftest-data")
endif()
