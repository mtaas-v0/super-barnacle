#!/usr/bin/env bash
set -euo pipefail

echo "========================================="
echo "   Blaze C Local Test Runner (Ubuntu 22.04)"
echo "========================================="

# 1. Native Linux Test Execution
echo "--> [1/2] Building & Running Native Linux Tests..."
cmake -B build-test-linux -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=ON

cmake --build build-test-linux --config Release

echo "--> Executing Linux test suite via CTest..."
ctest --test-dir build-test-linux --output-on-failure

# 2. Windows MinGW DLL + EXE Test via Wine (if installed)
echo ""
echo "--> [2/2] Building Windows x86_64 Targets..."
cmake -B build-test-windows -G Ninja \
    -DCMAKE_SYSTEM_NAME=Windows \
    -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc \
    -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=ON

cmake --build build-test-windows --config Release

if command -v wine64 &> /dev/null || command -v wine &> /dev/null; then
    WINE_CMD=$(command -v wine64 || command -v wine)
    echo "--> Running Windows test executable with Wine ($WINE_CMD)..."
    
    # Place DLL in the same directory as test_blaze_c.exe for Wine loader
    cp build-test-windows/libblaze_c.dll build-test-windows/bin/blaze_c.dll 2>/dev/null || true
    WINEDEBUG=-all "$WINE_CMD" build-test-windows/bin/test_blaze_c.exe
else
    echo "Notice: Wine not detected. Skipping emulation check (install via 'sudo apt install wine64')."
fi

echo ""
echo "All local test configurations passed successfully!"
