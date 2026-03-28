#!/bin/bash

# OverWrite Build Script
# For commercial production environments

set -e  # Exit on error

echo "=================================="
echo "OverWrite Build Script"
echo "=================================="

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Check if build directory exists
if [ -d "build" ]; then
    echo -e "${YELLOW}Build directory exists, cleaning...${NC}"
    rm -rf build
fi

# Create build directory
echo -e "${GREEN}Creating build directory...${NC}"
mkdir -p build
cd build

# Configure with CMake
echo -e "${GREEN}Configuring project with CMake...${NC}"
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build the project
echo -e "${GREEN}Building project...${NC}"
cmake --build . -j$(nproc)

# Check if build was successful
if [ $? -eq 0 ]; then
    echo -e "${GREEN}=================================="
    echo "Build completed successfully!"
    echo "=================================="
    echo -e "Run the game with: ${YELLOW}./OverWrite${NC}"
else
    echo -e "${RED}=================================="
    echo "Build failed!"
    echo "=================================="
    exit 1
fi