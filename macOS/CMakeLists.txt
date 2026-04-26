cmake_minimum_required(VERSION 3.20)
project(SimeIME LANGUAGES CXX Swift)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Source files ────────────────────────────────────────────────────────────

set(CXX_SOURCES
    # C API
    api/sime_api.cc
    # sime_core runtime (training tools live in CLI binaries, not the IME)
    ../src/sime.cc
    ../src/dict.cc
    ../src/score.cc
    ../src/trie.cc
    ../src/state.cc
    ../src/ustr.cc
)

set(SWIFT_SOURCES
    sources/Main.swift
    sources/SimeInputController.swift
    sources/SimePanel.swift
    sources/SimeView.swift
)

# ── Bundle target ────────────────────────────────────────────────────────────

add_executable(SimeIME MACOSX_BUNDLE
    ${CXX_SOURCES}
    ${SWIFT_SOURCES}
)

# Include paths for C++ headers
target_include_directories(SimeIME PRIVATE
    ../include
    api
)

# Swift bridging header
set_target_properties(SimeIME PROPERTIES
    XCODE_ATTRIBUTE_SWIFT_OBJC_BRIDGING_HEADER
        "${CMAKE_CURRENT_SOURCE_DIR}/api/sime_api.h"
    XCODE_ATTRIBUTE_SWIFT_INCLUDE_PATHS
        "${CMAKE_CURRENT_SOURCE_DIR}/api"
    MACOSX_BUNDLE_INFO_PLIST
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/Info.plist"
    MACOSX_BUNDLE_BUNDLE_NAME "是语"
    MACOSX_BUNDLE_BUNDLE_VERSION "1.0"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.sime.inputmethod.SimeIME"
)

# Link frameworks
target_link_libraries(SimeIME PRIVATE
    "-framework InputMethodKit"
    "-framework AppKit"
    "-framework Foundation"
    "-framework Carbon"
)

# C++20 and warnings
target_compile_options(SimeIME PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:
        -Wall -Wextra -pedantic -Wconversion
    >
)

# ── Install ──────────────────────────────────────────────────────────────────

install(TARGETS SimeIME
    BUNDLE DESTINATION "/Library/Input Methods"
)

# After install, register the input source:
#   /System/Library/CoreServices/InputMethodKit.framework/Versions/A/Resources/\
#     build_tools/imklaunchagent -r com.sime.inputmethod.SimeIME
# Then log out and back in, or:
#   sudo killall cfprefsd; sudo killall UserEventAgent
