#!/usr/bin/env bash
set -euo pipefail

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
    -c, --clean                        Wipe build and dist directories
    -d, --deps                         Install required APT dependencies
    -b, --build-type <Release|Debug>   Build configuration (default: Release)
    -j, --jobs <N>                     Number of parallel build jobs (default: $(nproc))
    -h, --help                         Show this help message
EOF
}

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

# Step 0: Dependencies
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

    sudo update-alternatives --set x86_64-w64-mingw32-g++ /usr/bin/x86_64-w64-mingw32-g++-posix
    sudo update-alternatives --set x86_64-w64-mingw32-gcc /usr/bin/x86_64-w64-mingw32-gcc-posix
fi

if [ "$CLEAN" = true ]; then
    echo "==> Cleaning previous build directories..."
    rm -rf build-host build-windows build-linux dist package
fi

# Step 1: Bootstrap native host codegen tool
bootstrap_host_tools() {
    echo "==> [Host Tools] Building native codegen tools on Linux..."
    cmake -B build-host -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_TESTING=OFF

    # Build both Unicode and IDNA codegen host targets
    cmake --build build-host --target sourcemeta_core_unicode_codegen sourcemeta_core_idna_codegen -j "${NUM_JOBS}"

    # Collect and add all directories containing built host binaries to PATH
    HOST_BIN_DIRS=$(find "$(pwd)/build-host" -type f -perm /111 -exec dirname {} + | sort -u | paste -sd ":" -)
    if [ -n "${HOST_BIN_DIRS}" ]; then
        export PATH="${HOST_BIN_DIRS}:${PATH}"
        echo "==> [Host Tools] Exported to PATH:"
        echo "${HOST_BIN_DIRS}" | tr ':' '\n'
    else
        echo "Error: Failed to locate compiled host codegen tools in build-host"
        exit 1
    fi
}

# Step 2: Cross-compile Windows Target (.dll)
build_windows() {
    bootstrap_host_tools

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

# Step 3: Build Linux Target (.so)
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
        echo "Error: Invalid target '$TARGET'"
        exit 1
        ;;
esac

echo "==> Build complete!"