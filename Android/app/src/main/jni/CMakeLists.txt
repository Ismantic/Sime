cmake_minimum_required(VERSION 3.22)
project(sime_jni LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Path to sime engine sources (relative to this CMakeLists.txt)
set(SIME_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../../../..)
set(SIME_SRC ${SIME_ROOT}/src)
set(SIME_INC ${SIME_ROOT}/include)

# sime_core as a static library
add_library(sime_core STATIC
    ${SIME_SRC}/ustr.cc
    ${SIME_SRC}/count.cc
    ${SIME_SRC}/construct.cc
    ${SIME_SRC}/convert.cc
    ${SIME_SRC}/compact.cc
    ${SIME_SRC}/score.cc
    ${SIME_SRC}/trie.cc
    ${SIME_SRC}/state.cc
    ${SIME_SRC}/sime.cc
    ${SIME_SRC}/dict.cc
    ${SIME_SRC}/cut.cc
    ${SIME_SRC}/cache.cc
)
target_include_directories(sime_core PUBLIC ${SIME_INC} ${SIME_SRC})
target_compile_options(sime_core PRIVATE -Wall -Wextra -O2)

# JNI shared library
add_library(sime_jni SHARED sime_jni.cc)
target_link_libraries(sime_jni PRIVATE sime_core log)
target_include_directories(sime_jni PRIVATE ${SIME_INC})
