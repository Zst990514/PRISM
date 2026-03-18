#!/bin/bash
# This script automates the CMake build process from the project root.

# Stop the script if any command fails
set -e

BUILD_DIR="HetPE_simulator/build"

echo "--- Cleaning up previous build directory: ${BUILD_DIR} ---"
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

echo "--- Starting build process in ${BUILD_DIR} ---"
cd "$BUILD_DIR"

echo "--> Running CMake..."
# We are in HetPE_simulator/build, CMakeLists.txt is one level up.
cmake ..

echo "--> Running Make..."
make

# Return to the root directory
cd ../..

echo "--- Build finished successfully! ---"
echo "Executables are located at: ${BUILD_DIR}/"
