#!/bin/bash

# OverWrite Shader Compilation Script
# Compiles GLSL shaders to SPIR-V format

set -e  # Exit on error

echo "=================================="
echo "Compiling OverWrite Shaders"
echo "=================================="

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Check if glslc is installed
if ! command -v glslc &> /dev/null; then
    echo -e "${RED}Error: glslc not found!${NC}"
    echo "Please install Vulkan SDK to get glslc compiler."
    exit 1
fi

# Create shaders directory if it doesn't exist
mkdir -p shaders

# Compile vertex shader
echo -e "${YELLOW}Compiling vertex shader...${NC}"
glslc shaders/shader.vert -o shaders/shader.vert.spv

if [ $? -eq 0 ]; then
    echo -e "${GREEN}Vertex shader compiled successfully${NC}"
else
    echo -e "${RED}Vertex shader compilation failed${NC}"
    exit 1
fi

# Compile fragment shader
echo -e "${YELLOW}Compiling fragment shader...${NC}"
glslc shaders/shader.frag -o shaders/shader.frag.spv

if [ $? -eq 0 ]; then
    echo -e "${GREEN}Fragment shader compiled successfully${NC}"
else
    echo -e "${RED}Fragment shader compilation failed${NC}"
    exit 1
fi

echo -e "${GREEN}=================================="
echo "All shaders compiled successfully!"
echo "=================================="