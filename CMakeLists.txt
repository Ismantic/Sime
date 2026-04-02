cmake_minimum_required(VERSION 3.20)
project(sime_new LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(NEW_ROOT ${CMAKE_CURRENT_LIST_DIR})
set(NEW_SRC_DIR ${NEW_ROOT}/src)
set(NEW_INC_DIR ${NEW_ROOT}/include)

add_library(sime_core
    ${NEW_SRC_DIR}/ustr.cc
    ${NEW_SRC_DIR}/unit.cc
    ${NEW_SRC_DIR}/count.cc
    ${NEW_SRC_DIR}/construct.cc
    ${NEW_SRC_DIR}/convert.cc
    ${NEW_SRC_DIR}/compact.cc
    ${NEW_SRC_DIR}/score.cc
    ${NEW_SRC_DIR}/trie.cc
    ${NEW_SRC_DIR}/state.cc
    ${NEW_SRC_DIR}/interpret.cc
    ${NEW_SRC_DIR}/dict.cc
    ${NEW_SRC_DIR}/nine.cc
    ${NEW_SRC_DIR}/context.cc
)

target_include_directories(sime_core PUBLIC ${NEW_INC_DIR} ${NEW_SRC_DIR})
target_compile_features(sime_core PUBLIC cxx_std_20)
target_compile_options(sime_core PRIVATE -Wall -Wextra -pedantic -Wconversion)
set_target_properties(sime_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_executable(sime-count ${NEW_ROOT}/app/counter.cc)
target_link_libraries(sime-count PRIVATE sime_core)

add_executable(sime-construct ${NEW_ROOT}/app/constructor.cc)
target_link_libraries(sime-construct PRIVATE sime_core)

add_executable(sime-converter ${NEW_ROOT}/app/converter.cc)
target_link_libraries(sime-converter PRIVATE sime_core)

add_executable(sime-compact ${NEW_ROOT}/app/compact.cc)
target_link_libraries(sime-compact PRIVATE sime_core)

add_executable(ime_interpreter ${NEW_ROOT}/app/interpreter.cc)
target_link_libraries(ime_interpreter PRIVATE sime_core)

# Compare tool (optional, requires libime)
find_library(LIBIME_PINYIN IMEPinyin)
find_library(LIBIME_CORE IMECore)
if(LIBIME_PINYIN AND LIBIME_CORE)
    add_executable(sime-compare ${NEW_ROOT}/app/compare.cc)
    target_link_libraries(sime-compare PRIVATE sime_core ${LIBIME_PINYIN} ${LIBIME_CORE})
    target_include_directories(sime-compare PRIVATE /usr/include/LibIME /usr/include/Fcitx5/Utils /usr/include/Fcitx5/Core)
endif()

set(_sime_targets
    sime-count
    sime-construct
    sime-converter
    sime-compact
    ime_interpreter
)

foreach(target_name IN LISTS _sime_targets)
    target_compile_options(${target_name} PRIVATE -Wall -Wextra -pedantic -Wconversion)
endforeach()

option(SIME_ENABLE_FCITX5 "Build fcitx5 input method plugin" OFF)
if(SIME_ENABLE_FCITX5)
    add_subdirectory(fcitx5)
endif()
