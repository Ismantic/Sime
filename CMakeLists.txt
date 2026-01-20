cmake_minimum_required(VERSION 3.20)
project(sime_new LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Optimization flags for Release builds
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -mtune=native -funroll-loops -finline-functions -ffast-math -flto -DNDEBUG" CACHE STRING "Release flags" FORCE)
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-flto" CACHE STRING "Release linker flags" FORCE)

# Debug flags
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g3 -ggdb" CACHE STRING "Debug flags" FORCE)

# Set default build type to Release if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
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
