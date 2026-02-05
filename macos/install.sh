#!/bin/bash
#
# SimeIME Installation Script for macOS
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
BUNDLE_NAME="SimeIME.app"
INSTALL_DIR="${HOME}/Library/Input Methods"

echo "======================================"
echo "SimeIME Installation Script"
echo "======================================"
echo ""

# Check if the bundle exists
if [ ! -d "${BUILD_DIR}/${BUNDLE_NAME}" ]; then
    echo "Error: ${BUNDLE_NAME} not found in ${BUILD_DIR}"
    echo ""
    echo "Please build the project first:"
    echo "  cd ${SCRIPT_DIR}"
    echo "  mkdir -p build && cd build"
    echo "  cmake .. -DCMAKE_BUILD_TYPE=Release"
    echo "  make -j\$(sysctl -n hw.ncpu)"
    echo ""
    exit 1
fi

# Check for data files
echo "Checking for data files..."
DATA_DIR="${BUILD_DIR}/${BUNDLE_NAME}/Contents/Resources"
mkdir -p "${DATA_DIR}"

DICT_FILE=""
LM_FILE=""

# Search for data files in various locations
SEARCH_PATHS=(
    "${SCRIPT_DIR}/.."  # Parent directory (Sime root)
    "${SCRIPT_DIR}/../.."  # Sime parent directory
    "${HOME}"
    "/usr/share/sunpinyin"
    "/usr/local/share/sunpinyin"
    "/opt/homebrew/share/sunpinyin"
)

for path in "${SEARCH_PATHS[@]}"; do
    if [ -z "${DICT_FILE}" ] && [ -f "${path}/pydict_sc.ime.bin" ]; then
        DICT_FILE="${path}/pydict_sc.ime.bin"
        echo "  Found dictionary: ${DICT_FILE}"
    fi
    if [ -z "${DICT_FILE}" ] && [ -f "${path}/pydict_sc.bin" ]; then
        # Convert the dictionary if needed
        echo "  Found raw dictionary: ${path}/pydict_sc.bin"
        if [ -f "${SCRIPT_DIR}/../trie_conv" ]; then
            echo "  Converting dictionary..."
            "${SCRIPT_DIR}/../trie_conv" --input "${path}/pydict_sc.bin" --output "${DATA_DIR}/pydict_sc.ime.bin"
            DICT_FILE="${DATA_DIR}/pydict_sc.ime.bin"
        fi
    fi
    if [ -z "${LM_FILE}" ] && [ -f "${path}/lm_sc.t3g" ]; then
        LM_FILE="${path}/lm_sc.t3g"
        echo "  Found language model: ${LM_FILE}"
    fi
done

if [ -z "${DICT_FILE}" ]; then
    echo ""
    echo "Warning: Dictionary file (pydict_sc.ime.bin or pydict_sc.bin) not found!"
    echo "The input method will not work without it."
    echo ""
    echo "Please install sunpinyin data files:"
    echo "  brew install sunpinyin"
    echo ""
    echo "Or download from: https://github.com/sunpinyin/sunpinyin"
    echo ""
fi

if [ -z "${LM_FILE}" ]; then
    echo ""
    echo "Warning: Language model file (lm_sc.t3g) not found!"
    echo "The input method will not work without it."
    echo ""
fi

# Copy data files to bundle
if [ -n "${DICT_FILE}" ] && [ -f "${DICT_FILE}" ]; then
    echo "  Copying dictionary to bundle..."
    cp "${DICT_FILE}" "${DATA_DIR}/"
fi

if [ -n "${LM_FILE}" ] && [ -f "${LM_FILE}" ]; then
    echo "  Copying language model to bundle..."
    cp "${LM_FILE}" "${DATA_DIR}/"
fi

# Install the bundle
echo ""
echo "Installing ${BUNDLE_NAME} to ${INSTALL_DIR}..."

# Create install directory if needed
mkdir -p "${INSTALL_DIR}"

# Remove old installation if exists
if [ -d "${INSTALL_DIR}/${BUNDLE_NAME}" ]; then
    echo "  Removing old installation..."
    rm -rf "${INSTALL_DIR}/${BUNDLE_NAME}"
fi

# Copy the bundle
cp -R "${BUILD_DIR}/${BUNDLE_NAME}" "${INSTALL_DIR}/"

echo "  Installed successfully!"
echo ""

# Register the input method
echo "Registering input method..."

# Touch the directory to notify the system
sudo touch "/Library/Input Methods" 2>/dev/null || true

# Kill InputMethodKit to force reload
killall InputMethodKit 2>/dev/null || true

echo ""
echo "======================================"
echo "Installation Complete!"
echo "======================================"
echo ""
echo "Next steps:"
echo ""
echo "1. Log out and log back in (or restart)"
echo ""
echo "2. Open System Settings -> Keyboard -> Input Sources"
echo ""
echo "3. Click '+' to add a new input source"
echo ""
echo "4. Find 'SimeIME' under 'Chinese, Simplified'"
echo ""
echo "5. Click 'Add' to enable the input method"
echo ""
echo "6. Switch to SimeIME using the input menu or keyboard shortcut"
echo ""
echo "To uninstall:"
echo "  rm -rf '${INSTALL_DIR}/${BUNDLE_NAME}'"
echo ""
