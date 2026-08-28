#!/usr/bin/env bash
set -euo pipefail

# Configuration
BUILD_TYPE="Release"
CLEAN=false
INSTALL_DEPS=false
NUM_JOBS=$(nproc)

# Toolchain version & cache paths
LLVM_MINGW_VERSION="20260826"
TOOLCHAIN_DIR="${HOME}/.cache/toolchains/llvm-mingw-${LLVM_MINGW_VERSION}-ucrt-ubuntu-22.04-x86_64"
LLVM_MINGW_URL="https://github.com/mstorsjo/llvm-mingw/releases/download/${LLVM_MINGW_VERSION}/llvm-mingw-${LLVM_MINGW_VERSION}-ucrt-ubuntu-22.04-x86_64.tar.xz"

#https://github.com/mstorsjo/llvm-mingw/releases/download/20260826/llvm-mingw-20260826-msvcrt-ubuntu-22.04-x86_64.tar.xz

print_usage() {
    cat << EOF
Usage: ./build-winArm64.sh [OPTIONS]

Cross-compiles Windows ARM64 (aarch64) DLL and packages with blaze_c.h.

LLVM_MINGW_URL=$LLVM_MINGW_URL

Options:
    -c, --clean                        Wipe build and packaging output directories
    -d, --deps                         Install base build dependencies via APT
    -b, --build-type <Release|Debug>   Build configuration (default: Release)
    -j, --jobs <N>                     Number of parallel build jobs (default: $(nproc))
    -h, --help                         Show this help message
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
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

# Step 1: Install host dependencies if requested
if [ "$INSTALL_DEPS" = true ]; then
    echo "==> Installing system dependencies..."
    sudo apt-get update
    sudo apt-get install -y \
        build-essential \
        cmake \
        ninja-build \
        git \
        curl \
        xz-utils
fi

# Step 2: Download & cache llvm-mingw ARM64 toolchain if not present
if [ ! -d "${TOOLCHAIN_DIR}/bin" ]; then
    echo "==> Fetching llvm-mingw toolchain for Windows ARM64 cross-compilation..."
    mkdir -p "$(dirname "${TOOLCHAIN_DIR}")"
    ARCHIVE_PATH="/tmp/llvm-mingw.tar.xz"
    
    curl -sSL "${LLVM_MINGW_URL}" -o "${ARCHIVE_PATH}"
    tar -xJf "${ARCHIVE_PATH}" -C "$(dirname "${TOOLCHAIN_DIR}")"
    rm -f "${ARCHIVE_PATH}"
    echo "==> Toolchain installed to: ${TOOLCHAIN_DIR}"
fi

export PATH="${TOOLCHAIN_DIR}/bin:${PATH}"

# Check that ARM64 Clang cross-compilers are available
CC_ARM64="${TOOLCHAIN_DIR}/bin/aarch64-w64-mingw32-clang"
CXX_ARM64="${TOOLCHAIN_DIR}/bin/aarch64-w64-mingw32-clang++"

if [ ! -f "${CXX_ARM64}" ]; then
    echo "Error: aarch64-w64-mingw32-clang++ compiler not found in ${TOOLCHAIN_DIR}/bin"
    exit 1
fi

# Step 2.5: Build Native Linux Codegen Tools
echo "==> Building native Linux host tools (Unicode & IDNA codegen)..."
cmake -B build-host -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=OFF

cmake --build build-host --target sourcemeta_core_unicode_codegen sourcemeta_core_idna_codegen -j "${NUM_JOBS}"

HOST_BIN_DIRS=$(find "$(pwd)/build-host" -type f -perm /111 -exec dirname {} + | sort -u | paste -sd ":" -)
export PATH="${HOST_BIN_DIRS}:${PATH}"

# Step 3: Clean up previous builds if requested
if [ "$CLEAN" = true ]; then
    echo "==> Cleaning build-win-arm64 and dist/windows-arm64..."
    rm -rf build-win-arm64 dist/windows-arm64 package/windows-arm64
fi

# Step 4: Configure CMake for Windows ARM64
echo "==> Configuring CMake for Windows ARM64 (aarch64-w64-mingw32)..."
cmake -B build-win-arm64 -G Ninja \
    -DCMAKE_SYSTEM_NAME=Windows \
    -DCMAKE_SYSTEM_PROCESSOR=ARM64 \
    -DCMAKE_C_COMPILER="${CC_ARM64}" \
    -DCMAKE_CXX_COMPILER="${CXX_ARM64}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DCMAKE_INSTALL_PREFIX="dist/windows-arm64" \
    -DCMAKE_CXX_FLAGS="-static-libgcc -static-libstdc++"

# Step 5: Build and install
echo "==> Building Windows ARM64 DLL..."
cmake --build build-win-arm64 --config "${BUILD_TYPE}" --target install -j "${NUM_JOBS}"

# Step 6: Package artifacts
mkdir -p package/windows-arm64
cp dist/windows-arm64/bin/*.dll package/windows-arm64/ 2>/dev/null || true
cp dist/windows-arm64/lib/*.a package/windows-arm64/ 2>/dev/null || true
cp dist/windows-arm64/include/blaze_c.h package/windows-arm64/

echo "==> [Success] Windows ARM64 build packaged into ./package/windows-arm64/:"
ls -lh package/windows-arm64/
