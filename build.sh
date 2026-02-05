#!/bin/bash

# Build script for TuTraSt C++ implementation
# Usage: ./build.sh [clean|debug|release]

set -e  # Exit on error

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
BUILD_DIR="$SCRIPT_DIR/build"

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

print_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

# Clean build directory
if [ "$1" = "clean" ]; then
    print_info "Cleaning build directory..."
    rm -rf "$BUILD_DIR"
    print_info "Clean complete"
    exit 0
fi

# Check for CMake
if ! command -v cmake &> /dev/null; then
    print_error "CMake is not installed. Please install CMake 3.10 or higher."
    exit 1
fi

# Check CMake version
CMAKE_VERSION=$(cmake --version | grep -oP '\d+\.\d+' | head -1)
CMAKE_MAJOR=$(echo $CMAKE_VERSION | cut -d. -f1)
CMAKE_MINOR=$(echo $CMAKE_VERSION | cut -d. -f2)

if [ "$CMAKE_MAJOR" -lt 3 ] || ([ "$CMAKE_MAJOR" -eq 3 ] && [ "$CMAKE_MINOR" -lt 10 ]); then
    print_error "CMake version $CMAKE_VERSION is too old. Please install CMake 3.10 or higher."
    exit 1
fi

# Check for C++ compiler
if ! command -v g++ &> /dev/null && ! command -v clang++ &> /dev/null; then
    print_error "No C++ compiler found. Please install g++ or clang++."
    exit 1
fi

# Create build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Determine build type
BUILD_TYPE="Release"
if [ "$1" = "debug" ]; then
    BUILD_TYPE="Debug"
    print_info "Building in Debug mode..."
else
    print_info "Building in Release mode..."
fi

# Configure with CMake
print_info "Configuring with CMake..."
cmake -DCMAKE_BUILD_TYPE=$BUILD_TYPE ..

# Build
print_info "Compiling..."
make -j$(nproc)

# Check if build succeeded
if [ -f "$BUILD_DIR/tutrast" ]; then
    print_info "Build successful!"
    print_info "Executable location: $BUILD_DIR/tutrast"
    print_info ""
    print_info "To run the program:"
    print_info "  cd $SCRIPT_DIR"
    print_info "  ./build/tutrast"
    print_info ""
    print_info "To install system-wide (optional):"
    print_info "  cd $BUILD_DIR"
    print_info "  sudo make install"
else
    print_error "Build failed!"
    exit 1
fi
