#!/bin/bash
set -e

echo "Building EMPI Agent HTTP Server..."

# Build
mkdir -p build
cd build
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_HTTP=ON \
    -DBUILD_GUI=OFF \
    -DBUILD_TESTS=OFF
make -j$(nproc)

echo "Build complete!"
echo "Run: ./build/empi_http -c config/agent_config.toml -p 8080"
