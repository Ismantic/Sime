cmake_minimum_required(VERSION 3.20)
project(sime_new LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Performance optimization options
option(SIME_ENABLE_SIMD "Enable SIMD (AVX2) optimizations" ON)
option(SIME_ENABLE_PGO_GENERATE "Generate PGO profile data" OFF)
option(SIME_ENABLE_PGO_USE "Use PGO profile data for optimization" OFF)

# Fcitx5 integration
option(SIME_ENABLE_FCITX5 "Build fcitx5 input method plugin" ON)

# Optimization flags for Release builds
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -mtune=native -funroll-loops -finline-functions -ffast-math -flto -DNDEBUG" CACHE STRING "Release flags" FORCE)
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-flto" CACHE STRING "Release linker flags" FORCE)

# Debug flags
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g3 -ggdb" CACHE STRING "Debug flags" FORCE)

# Set default build type to Release if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# PGO support
if(SIME_ENABLE_PGO_GENERATE AND SIME_ENABLE_PGO_USE)
    message(FATAL_ERROR "Cannot enable both PGO_GENERATE and PGO_USE simultaneously")
endif()

if(SIME_ENABLE_PGO_GENERATE)
    message(STATUS "PGO: Instrumentation build - will generate profile data")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fprofile-generate")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fprofile-generate")
    set(PGO_PROFILE_DIR "${CMAKE_BINARY_DIR}/pgo-profiles" CACHE PATH "Directory for PGO profile data")
    file(MAKE_DIRECTORY ${PGO_PROFILE_DIR})
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fprofile-dir=${PGO_PROFILE_DIR}")
elseif(SIME_ENABLE_PGO_USE)
    message(STATUS "PGO: Optimized build - using profile data")
    set(PGO_PROFILE_DIR "${CMAKE_BINARY_DIR}/pgo-profiles" CACHE PATH "Directory for PGO profile data")
    if(NOT EXISTS ${PGO_PROFILE_DIR})
        message(WARNING "PGO profile directory does not exist: ${PGO_PROFILE_DIR}")
    endif()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fprofile-use -fprofile-correction -fprofile-dir=${PGO_PROFILE_DIR}")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fprofile-use -fprofile-dir=${PGO_PROFILE_DIR}")
endif()

set(NEW_ROOT ${CMAKE_CURRENT_LIST_DIR})
set(NEW_SRC_DIR ${NEW_ROOT}/src)
set(NEW_INC_DIR ${NEW_ROOT}/include)

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Release flags: ${CMAKE_CXX_FLAGS_RELEASE}")
message(STATUS "C++ Debug flags: ${CMAKE_CXX_FLAGS_DEBUG}")

add_library(sime_core
    ${NEW_SRC_DIR}/ustr.cc
    ${NEW_SRC_DIR}/sentence.cc
    ${NEW_SRC_DIR}/dict.cc
    ${NEW_SRC_DIR}/unit.cc
    ${NEW_SRC_DIR}/segment.cc
    ${NEW_SRC_DIR}/count.cc
    ${NEW_SRC_DIR}/construct.cc
    ${NEW_SRC_DIR}/convert.cc
    ${NEW_SRC_DIR}/score.cc
    ${NEW_SRC_DIR}/trie.cc
    ${NEW_SRC_DIR}/state.cc
    ${NEW_SRC_DIR}/interpret.cc
    ${NEW_SRC_DIR}/cut.cc
)

target_include_directories(sime_core PUBLIC ${NEW_INC_DIR} ${NEW_SRC_DIR})
target_compile_features(sime_core PUBLIC cxx_std_20)
target_compile_options(sime_core PRIVATE -Wall -Wextra -pedantic -Wconversion)

# SIMD optimizations
if(SIME_ENABLE_SIMD)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
        message(STATUS "SIMD: Enabling AVX2 optimizations for x86_64")
        target_compile_options(sime_core PRIVATE -mavx2 -mfma)
        target_compile_definitions(sime_core PRIVATE SIME_HAS_AVX2=1)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|ARM64")
        message(STATUS "SIMD: Enabling NEON optimizations for ARM64")
        target_compile_options(sime_core PRIVATE -march=armv8-a+simd)
        target_compile_definitions(sime_core PRIVATE SIME_HAS_NEON=1)
    else()
        message(STATUS "SIMD: No SIMD support for processor ${CMAKE_SYSTEM_PROCESSOR}")
    endif()
else()
    message(STATUS "SIMD: Disabled")
endif()

add_executable(sime-segment ${NEW_ROOT}/app/segmenter.cc)
target_link_libraries(sime-segment PRIVATE sime_core)

add_executable(sime-count ${NEW_ROOT}/app/counter.cc)
target_link_libraries(sime-count PRIVATE sime_core)

add_executable(sime-construct ${NEW_ROOT}/app/constructor.cc)
target_link_libraries(sime-construct PRIVATE sime_core)

add_executable(sime-converter ${NEW_ROOT}/app/converter.cc)
target_link_libraries(sime-converter PRIVATE sime_core)

add_executable(ime_interpreter ${NEW_ROOT}/app/interpreter.cc)
target_link_libraries(ime_interpreter PRIVATE sime_core)

add_executable(ime_cutter ${NEW_ROOT}/app/cutter.cc)
target_link_libraries(ime_cutter PRIVATE sime_core)

set(_sime_targets
    sime-segment
    sime-count
    sime-construct
    sime-converter
    ime_interpreter
    ime_cutter
)

foreach(target_name IN LISTS _sime_targets)
    target_compile_options(${target_name} PRIVATE -Wall -Wextra -pedantic -Wconversion)
endforeach()

# Fcitx5 input method plugin
if(SIME_ENABLE_FCITX5)
    add_subdirectory(fcitx5)
endif()
