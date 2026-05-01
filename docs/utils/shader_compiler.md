# 着色器编译器

着色器编译器负责将 GLSL 源代码编译为 SPIR-V 字节码。

---

## ShaderCompiler

着色器编译器类，提供静态编译方法。

### 类定义

```cpp
class ShaderCompiler {
public:
    // 编译着色器并返回 SPIR-V 字节码
    static std::vector<char> compileShader(const std::string& sourceFile, 
                                           const std::string& stage);
    
    // 编译着色器并保存到文件
    static bool compileToSpirV(const std::string& sourceFile,
                               const std::string& outputFile,
                               const std::string& stage);
};
```

### 方法说明

#### compileShader

编译着色器源文件并返回 SPIR-V 字节码。

```cpp
std::vector<char> compileShader(const std::string& sourceFile, const std::string& stage);
```

**参数：**
- `sourceFile`: GLSL 源文件路径（如 `shader.vert`）
- `stage`: 着色器阶段（`vert`, `frag`, `comp` 等）

**返回：**
- SPIR-V 字节码向量

**异常：**
- 编译失败时返回空向量

#### compileToSpirV

编译着色器并保存到文件。

```cpp
bool compileToSpirV(const std::string& sourceFile,
                   const std::string& outputFile,
                   const std::string& stage);
```

**参数：**
- `sourceFile`: GLSL 源文件路径
- `outputFile`: 输出 SPIR-V 文件路径
- `stage`: 着色器阶段

**返回：**
- 编译成功返回 `true`，失败返回 `false`

---

## 使用示例

### 运行时编译

```cpp
// 头文件路径说明：ShaderCompiler 位于 include/core/shader_compiler.h，请在代码中使用 include/core/shader_compiler.h 引用头文件
using namespace owengine;

// 编译顶点着色器
auto vertShader = ShaderCompiler::compileShader("shaders/shader.vert", "vert");
if (vertShader.empty()) {
    Logger::error("Failed to compile vertex shader");
}

// 编译片段着色器
auto fragShader = ShaderCompiler::compileShader("shaders/shader.frag", "frag");
if (fragShader.empty()) {
    Logger::error("Failed to compile fragment shader");
}

// 创建着色器模块
VkShaderModule vertModule = createShaderModule(device, vertShader);
VkShaderModule fragModule = createShaderModule(device, fragShader);
```

### 预编译

```cpp
// 编译并保存
if (ShaderCompiler::compileToSpirV("shaders/shader.vert", "shaders/shader.vert.spv", "vert")) {
    Logger::info("Vertex shader compiled successfully");
}
```

---

## 着色器阶段

支持的着色器阶段：

| 阶段标识 | 描述 | GLSL 后缀 |
|----------|------|-----------|
| `vert` | 顶点着色器 | `.vert` |
| `frag` | 片段着色器 | `.frag` |
| `geom` | 几何着色器 | `.geom` |
| `comp` | 计算着色器 | `.comp` |
| `tesc` | 细分控制着色器 | `.tesc` |
| `tese` | 细分评估着色器 | `.tese` |

---

## 项目中的使用

项目提供了 `compile_shaders.sh` 脚本用于预编译所有着色器：

```bash
#!/bin/bash
# compile_shaders.sh

glslangValidator -V shaders/shader.vert -o shaders/shader.vert.spv
glslangValidator -V shaders/shader.frag -o shaders/shader.frag.spv
glslangValidator -V shaders/skybox.vert -o shaders/skybox.vert.spv
glslangValidator -V shaders/skybox.frag -o shaders/skybox.frag.spv
```

### VulkanPipeline 中的使用

```cpp
// vulkan_pipeline.cpp
std::vector<char> VulkanPipeline::readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + filename);
    }
    
    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);
    
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();
    
    return buffer;
}

VkShaderModule VulkanPipeline::createShaderModule(const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
    
    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device->getDevice(), &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shader module");
    }
    
    return shaderModule;
}
```

---

## GLSL 示例

### 顶点着色器

```glsl
// shader.vert
#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inTexCoord;

layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec3 baseColor;
    float metallic;
    float roughness;
    int hasTexture;
} pc;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragNormal;
layout(location = 3) out vec3 fragPos;

void main() {
    gl_Position = pc.proj * pc.view * pc.model * vec4(inPosition, 1.0);
    fragColor = inColor * pc.baseColor;
    fragTexCoord = inTexCoord;
    fragNormal = mat3(transpose(inverse(pc.model))) * inNormal;
    fragPos = vec3(pc.model * vec4(inPosition, 1.0));
}
```

### 片段着色器

```glsl
// shader.frag
#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec3 fragPos;

layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec3 baseColor;
    float metallic;
    float roughness;
    int hasTexture;
} pc;

layout(binding = 0) uniform sampler2D texSampler;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 color = fragColor;
    
    if (pc.hasTexture == 1) {
        color *= texture(texSampler, fragTexCoord).rgb;
    }
    
    // 简单光照
    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
    float diff = max(dot(normalize(fragNormal), lightDir), 0.0);
    vec3 ambient = vec3(0.1);
    
    outColor = vec4((ambient + diff) * color, 1.0);
}
```

---

## 依赖

着色器编译需要以下工具之一：

1. **glslangValidator** - Khronos 官方 GLSL 编译器
2. **glslc** - Google 的 SPIR-V 编译器

### 安装

**Arch Linux:**
```bash
sudo pacman -S glslang shaderc
```

**Ubuntu/Debian:**
```bash
sudo apt install glslang-tools shaderc
```

---

## 调试

### 验证着色器

```bash
# 检查语法
glslangValidator shaders/shader.vert

# 查看编译后的 SPIR-V
spirv-dis shaders/shader.vert.spv
```

### 运行时错误

Vulkan 验证层会在着色器出错时提供详细错误信息：

```
validation layer: SPIR-V module not valid: 
  OpEntryPoint Entry Point requires capability Sampled1D
```

---

## 最佳实践

1. **预编译**：发布版本使用预编译的 SPIR-V 文件
2. **调试时运行时编译**：开发时可以使用运行时编译便于快速迭代
3. **版本控制**：将 `.spv` 文件加入 `.gitignore`，通过 CI 构建
