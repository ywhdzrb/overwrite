# OverWrite

A modern Vulkan-based 3D game engine framework built for commercial production environments.

## Features

- **Vulkan API**: Low-level graphics API for maximum performance
- **3D Model Loading**: Support for OBJ model format
- **Modern Rendering Pipeline**: Optimized for production use
- **Cross-platform**: Linux support with Windows/Mac planned
- **Commercial-grade**: Designed for production environments

## Requirements

- **Operating System**: Linux (x86_64)
- **Vulkan SDK**: 1.3 or higher
- **CMake**: 3.20 or higher
- **C++ Compiler**: GCC 10+ or Clang 12+
- **GLFW**: Latest version
- **GLM**: OpenGL Mathematics library
- **Vulkan Headers**: Latest version
- **Vulkan Loader**: Latest version
- **TinyOBJLoader**: For OBJ model loading

## Installation

### Prerequisites

Install required dependencies on Linux (Arch/Manjaro):

```bash
sudo pacman -S vulkan-headers vulkan-tools glfw glm cmake gcc
```

Or on Ubuntu/Debian:

```bash
sudo apt install vulkan-sdk libglfw3-dev libglm-dev cmake build-essential
```

### Building the Project

1. Clone the repository:
```bash
git clone <repository-url>
cd OverWrite
```

2. Create build directory:
```bash
mkdir build
cd build
```

3. Configure with CMake:
```bash
cmake ..
```

4. Build:
```bash
cmake --build .
```

5. Run the game:
```bash
./OverWrite
```

## Project Structure

```
OverWrite/
├── CMakeLists.txt          # Build configuration
├── LICENSE                 # GPL v3 for code, CC BY-NC-ND for art
├── README.md              # This file
├── include/               # Header files
│   ├── vulkan_instance.h
│   ├── vulkan_device.h
│   ├── vulkan_swapchain.h
│   ├── vulkan_render_pass.h
│   ├── vulkan_pipeline.h
│   ├── vulkan_framebuffer.h
│   ├── vulkan_command_buffer.h
│   ├── vulkan_sync.h
│   ├── renderer.h
│   ├── model.h
│   ├── mesh.h
│   ├── shader_compiler.h
│   └── logger.h
├── src/                   # Source files
│   ├── core/              # Vulkan core components
│   ├── renderer/          # Rendering system
│   ├── utils/             # Utility functions
│   └── main.cpp           # Entry point
├── shaders/               # GLSL shaders
│   ├── shader.vert        # Vertex shader
│   └── shader.frag        # Fragment shader
└── assets/                # Game assets
    ├── models/            # 3D models
    └── textures/          # Textures
```

## Usage

### Loading 3D Models

Place your OBJ models in the `assets/models/` directory and load them using the Model class:

```cpp
Model model(vulkanDevice);
model.loadFromObj("assets/models/your_model.obj");
```

### Custom Shaders

Shaders are written in GLSL and compiled to SPIR-V using `glslc`. Place your shaders in the `shaders/` directory.

## License

### Code
This project is licensed under the GNU General Public License v3.0. See [LICENSE](LICENSE) for details.

### Artistic Content
All artistic content (models, textures, sounds, etc.) is licensed under Creative Commons Attribution-NonCommercial-NoDerivatives 4.0 International License.

## Contributing

Contributions are welcome! Please ensure your code follows the existing style and includes appropriate documentation.

## Performance

This engine is optimized for commercial production environments:
- Efficient Vulkan resource management
- Multi-threaded rendering preparation
- Memory-mapped vertex/index buffers
- Optimized shader compilation pipeline

## Troubleshooting

### Vulkan Validation Layers
For development, enable validation layers for detailed error reporting. In production, build with `-DNDEBUG` to disable them.

### Shader Compilation
Ensure `glslc` is installed and in your PATH for automatic shader compilation during build.

## Roadmap

- [ ] Windows support
- [ ] macOS support
- [ ] Additional model formats (FBX, GLTF)
- [ ] Advanced lighting (PBR)
- [ ] Post-processing effects
- [ ] Physics engine integration
- [ ] Audio system
- [ ] UI system

## Contact

For questions or support, please open an issue on the project repository.

## Acknowledgments

- Vulkan SDK and documentation
- GLFW for window management
- GLM for mathematics
- TinyOBJLoader for model loading
- The Vulkan community for excellent resources and support