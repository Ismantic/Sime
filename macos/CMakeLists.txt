#
# CMake build for Sime macOS Input Method
#

cmake_minimum_required(VERSION 3.20)
project(SimeIME VERSION 1.0.0 LANGUAGES CXX OBJCXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OBJCXX_STANDARD 20)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)

# Find required frameworks
find_library(FOUNDATION_LIBRARY Foundation REQUIRED)
find_library(COCOA_LIBRARY Cocoa REQUIRED)
find_library(INPUT_METHOD_KIT_LIBRARY InputMethodKit REQUIRED)

# Include directories
set(SIME_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(SIME_INCLUDE_DIR ${SIME_ROOT}/include)
set(SIME_SRC_DIR ${SIME_ROOT}/src)

# Source files for the core engine
set(SIME_CORE_SOURCES
    ${SIME_SRC_DIR}/ustr.cc
    ${SIME_SRC_DIR}/sentence.cc
    ${SIME_SRC_DIR}/dict.cc
    ${SIME_SRC_DIR}/unit.cc
    ${SIME_SRC_DIR}/segment.cc
    ${SIME_SRC_DIR}/count.cc
    ${SIME_SRC_DIR}/construct.cc
    ${SIME_SRC_DIR}/convert.cc
    ${SIME_SRC_DIR}/score.cc
    ${SIME_SRC_DIR}/trie.cc
    ${SIME_SRC_DIR}/state.cc
    ${SIME_SRC_DIR}/interpret.cc
    ${SIME_SRC_DIR}/cut.cc
    ${SIME_SRC_DIR}/selection_manager.cc
)

# Input Method sources
set(SIME_IME_SOURCES
    SimeIME/SimeIME/SimeEngine.mm
    SimeIME/SimeIME/SimeInputController.mm
    SimeIME/SimeIME/SimeCandidatesWindow.mm
    SimeIME/SimeIME/main.m
)

# Create the input method bundle
set(BUNDLE_NAME "SimeIME")
set(BUNDLE_IDENTIFIER "com.ismantic.SimeIME")

add_executable(${BUNDLE_NAME} MACOSX_BUNDLE
    ${SIME_CORE_SOURCES}
    ${SIME_IME_SOURCES}
)

# Set bundle properties
set_target_properties(${BUNDLE_NAME} PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/SimeIME/SimeIME/Info.plist
    MACOSX_BUNDLE_BUNDLE_NAME ${BUNDLE_NAME}
    MACOSX_BUNDLE_BUNDLE_VERSION "1.0.0"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0.0"
    MACOSX_BUNDLE_GUI_IDENTIFIER ${BUNDLE_IDENTIFIER}
    MACOSX_BUNDLE_ICON_FILE ""
    XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
    XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_WEAK YES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)

# Include directories
target_include_directories(${BUNDLE_NAME} PRIVATE
    ${SIME_INCLUDE_DIR}
    ${SIME_SRC_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/SimeIME/SimeIME
)

# Compile options
target_compile_options(${BUNDLE_NAME} PRIVATE
    -Wall
    -Wextra
    -Wconversion
    -Wno-sign-conversion
    -Wno-unused-parameter
)

# Link libraries
target_link_libraries(${BUNDLE_NAME} PRIVATE
    ${FOUNDATION_LIBRARY}
    ${COCOA_LIBRARY}
    ${INPUT_METHOD_KIT_LIBRARY}
    "-lc++"
)

# macOS deployment target
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version" FORCE)

# Generate menubar .icns file on macOS (only if iconutil is available)
if(APPLE)
    find_program(ICONUTIL iconutil)
    if(ICONUTIL)
        add_custom_command(TARGET ${BUNDLE_NAME} PRE_BUILD
            COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/../logo/generate_menubar_icns.sh
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../logo
            COMMENT "Generating menubar icon (.icns) from rectangular logos"
        )
    else()
        message(WARNING "iconutil not found - menubar icon will not be regenerated")
    endif()
endif()

# Copy icon files to Resources
add_custom_command(TARGET ${BUNDLE_NAME} POST_BUILD
    # Copy main app icon (square)
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/../logo/sime_minimal.icns
        $<TARGET_BUNDLE_CONTENT_DIR:${BUNDLE_NAME}>/Resources/sime.icns
    # Copy menubar icon (rectangular) if it exists
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/../logo/sime_menubar.icns
        $<TARGET_BUNDLE_CONTENT_DIR:${BUNDLE_NAME}>/Resources/sime_menubar.icns
    # Fallback: Copy rectangular PNG for menubar
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/../logo/sime_minimal_macos_29x22.png
        $<TARGET_BUNDLE_CONTENT_DIR:${BUNDLE_NAME}>/Resources/sime_menubar.png
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/../logo/sime_minimal_macos_44x34.png
        $<TARGET_BUNDLE_CONTENT_DIR:${BUNDLE_NAME}>/Resources/sime_menubar@2x.png
    COMMENT "Copying icon files to bundle"
)

# Create install target for the input method
install(TARGETS ${BUNDLE_NAME}
    BUNDLE DESTINATION ~/Library/Input\ Methods
)

# Print configuration message
message(STATUS "SimeIME Configuration:")
message(STATUS "  Bundle Name: ${BUNDLE_NAME}")
message(STATUS "  Bundle Identifier: ${BUNDLE_IDENTIFIER}")
message(STATUS "  Install Location: ~/Library/Input Methods")
