#!/usr/bin/env bash
set -euo pipefail

# Default options
TARGET="windows"
BUILD_TYPE="Release"
CLEAN=false
INSTALL_DEPS=false
NUM_JOBS=$(nproc)

print_usage() {
    cat << EOF
Usage: ./build.sh [OPTIONS]

Options:
    -t, --target <windows|linux|all>   Target platform to build (default: windows)
    -c, --clean                        Wipe build and dist directories before building
    -d, --deps                         Install required APT dependencies (requires sudo)
    -b, --build-type <Release|Debug>   Build configuration (default: Release)
    -j, --jobs <N>                     Number of parallel build jobs (default: $(nproc))
    -h, --help                         Show this help message
EOF
}

# Parse command-line arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        -t|--target)
            TARGET="$2"
            shift 2
            ;;
        -c|--clean)
            CLEAN=true
            shift
            ;;
        -d|--deps)
            INSTALL_DEPS=true
            shift
            ;;
        -b|--build-type)
            BUILD_TYPE="$2"
            shift 2
            ;;
        -j|--jobs)
            NUM_JOBS="$2"
            shift 2
            ;;
        -h|--help)
            print_usage
            exit 0
            ;;
        *)
            echo "Error: Unknown argument $1"
            print_usage
            exit 1
            ;;
    esac
done

# Step 0: Install dependencies if requested
if [ "$INSTALL_DEPS" = true ]; then
    echo "==> Installing system dependencies via APT..."
    sudo apt-get update
    sudo apt-get install -y \
        build-essential \
        cmake \
        ninja-build \
        git \
        mingw-w64 \
        g++-mingw-w64-x86-64-posix \
        gcc-mingw-w64-x86-64-posix

    # Ensure MinGW uses POSIX threads alternative for C++20 standard library support
    sudo update-alternatives --set x86_64-w64-mingw32-g++ /usr/bin/x86_64-w64-mingw32-g++-posix
    sudo update-alternatives --set x86_64-w64-mingw32-gcc /usr/bin/x86_64-w64-mingw32-gcc-posix
fi

# Clean builds if requested
if [ "$CLEAN" = true ]; then
    echo "==> Cleaning previous build directories..."
    rm -rf build-windows build-linux dist package
fi

# Function: Build Windows Target (.dll)
build_windows() {
    echo "==> [Windows DLL] Configuring CMake with MinGW toolchain..."
    cmake -B build-windows -G Ninja \
        -DCMAKE_SYSTEM_NAME=Windows \
        -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc \
        -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ \
        -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
        -DCMAKE_INSTALL_PREFIX=dist/windows

    echo "==> [Windows DLL] Building and installing..."
    cmake --build build-windows --config "${BUILD_TYPE}" --target install -j "${NUM_JOBS}"

    mkdir -p package/windows
    cp dist/windows/bin/*.dll package/windows/ 2>/dev/null || true
    cp dist/windows/lib/*.a package/windows/ 2>/dev/null || true
    cp dist/windows/include/blaze_c.h package/windows/

    echo "==> [Windows DLL] Successfully packaged in ./package/windows/"
}

# Function: Build Linux Target (.so)
build_linux() {
    echo "==> [Linux Shared Object] Configuring CMake..."
    cmake -B build-linux -G Ninja \
        -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
        -DCMAKE_INSTALL_PREFIX=dist/linux

    echo "==> [Linux Shared Object] Building and installing..."
    cmake --build build-linux --config "${BUILD_TYPE}" --target install -j "${NUM_JOBS}"

    mkdir -p package/linux
    cp dist/linux/lib/*.so* package/linux/ 2>/dev/null || true
    cp dist/linux/include/blaze_c.h package/linux/

    echo "==> [Linux Shared Object] Successfully packaged in ./package/linux/"
}

# Execution Dispatcher
case "$TARGET" in
    windows)
        build_windows
        ;;
    linux)
        build_linux
        ;;
    all)
        build_windows
        build_linux
        ;;
    *)
        echo "Error: Invalid target '$TARGET'. Use 'windows', 'linux', or 'all'."
        exit 1
        ;;
esac

echo "==> Build complete!"