# 纹理系统

纹理系统提供纹理加载和管理功能。

## 类概览

| 类名 | 文件 | 描述 |
|------|------|------|
| `Texture` | `include/texture.h` | 纹理资源类 |
| `TextureLoader` | `include/texture_loader.h` | 纹理加载器 |

---

## Texture

封装 Vulkan 纹理资源，包括图像、视图和采样器。

### 类方法

#### 构造与析构

```cpp
Texture(std::shared_ptr<class VulkanDevice> device,
        uint32_t width,
        uint32_t height,
        VkFormat format = VK_FORMAT_R8G8B8A8_SRGB,
        uint32_t mipLevels = 1);
~Texture();

// 禁止拷贝
Texture(const Texture&) = delete;
Texture& operator=(const Texture&) = delete;
```

#### 创建

```cpp
// 从图像数据创建纹理
void createFromData(const unsigned char* pixels, 
                    size_t imageSize, 
                    int channelCount);

// 生成 Mipmap
void generateMipmaps();
```

#### 获取器

```cpp
VkImage getImage() const;
VkImageView getImageView() const;
VkSampler getSampler() const;
uint32_t getWidth() const;
uint32_t getHeight() const;
uint32_t getMipLevels() const;
VkFormat getFormat() const;
```

#### 采样器参数

```cpp
void setSamplerParameters(VkFilter filterMode,
                         VkSamplerAddressMode addressMode,
                         bool anisotropyEnabled = true,
                         float maxAnisotropy = 16.0f);
```

### 使用示例

```cpp
// 创建纹理
auto texture = std::make_shared<Texture>(device, 512, 512);

// 加载图像数据
int width, height, channels;
unsigned char* data = stbi_load("texture.png", &width, &height, &channels, 4);

// 创建纹理
texture->createFromData(data, width * height * 4, 4);
stbi_image_free(data);

// 设置采样器参数
texture->setSamplerParameters(
    VK_FILTER_LINEAR,
    VK_SAMPLER_ADDRESS_MODE_REPEAT,
    true,
    16.0f
);

// 生成 Mipmap
texture->generateMipmaps();
```

---

## TextureLoader

纹理加载器，负责从文件加载纹理并管理缓存。

### 类方法

#### 构造

```cpp
explicit TextureLoader(std::shared_ptr<VulkanDevice> device);
~TextureLoader() = default;

// 禁止拷贝
TextureLoader(const TextureLoader&) = delete;
TextureLoader& operator=(const TextureLoader&) = delete;
```

#### 加载纹理

```cpp
// 从文件加载纹理
// 支持格式：PNG, JPG, BMP, TGA, PSD, GIF, HDR, PIC, PNM
std::shared_ptr<Texture> loadTexture(const std::string& filename, bool useCache = true);

// 从内存加载纹理
std::shared_ptr<Texture> loadTextureFromMemory(const unsigned char* data,
                                               size_t size,
                                               int width,
                                               int height,
                                               int channelCount);

// 创建空纹理
std::shared_ptr<Texture> createEmptyTexture(uint32_t width,
                                            uint32_t height,
                                            VkFormat format = VK_FORMAT_R8G8B8A8_SRGB);
```

#### 卸载纹理

```cpp
// 卸载指定纹理
bool unloadTexture(const std::string& filename);

// 卸载所有纹理
void unloadAllTextures();

// 清空缓存
void clearCache();
```

#### 缓存管理

```cpp
// 获取缓存的纹理数量
size_t getCacheSize() const;

// 检查纹理是否已加载
bool isTextureLoaded(const std::string& filename) const;

// 获取缓存的纹理
std::shared_ptr<Texture> getTexture(const std::string& filename) const;
```

#### 参数设置

```cpp
// 设置默认采样器参数
void setDefaultSamplerParameters(VkFilter filterMode,
                                 VkSamplerAddressMode addressMode,
                                 bool anisotropyEnabled = true,
                                 float maxAnisotropy = 16.0f);

// Mipmap 生成
void setGenerateMipmaps(bool enable);
bool isGenerateMipmapsEnabled() const;
```

---

## 使用示例

### 基本加载

```cpp
// 创建纹理加载器
auto textureLoader = std::make_shared<TextureLoader>(device);

// 加载纹理（自动缓存）
auto texture = textureLoader->loadTexture("assets/textures/diffuse.png");

// 检查是否加载成功
if (!texture) {
    std::cerr << "Failed to load texture" << std::endl;
}
```

### 缓存管理

```cpp
// 第一次加载（从文件读取）
auto tex1 = textureLoader->loadTexture("texture.png");

// 第二次加载（从缓存获取）
auto tex2 = textureLoader->loadTexture("texture.png");  // 返回同一实例

// 检查缓存
if (textureLoader->isTextureLoaded("texture.png")) {
    std::cout << "Texture is cached" << std::endl;
}

// 获取缓存数量
std::cout << "Cached textures: " << textureLoader->getCacheSize() << std::endl;

// 清空缓存
textureLoader->clearCache();
```

### 从内存加载

```cpp
// 假设 data 是图像数据
std::vector<unsigned char> imageData = loadImageData();

auto texture = textureLoader->loadTextureFromMemory(
    imageData.data(),
    imageData.size(),
    512,  // width
    512,  // height
    4     // channels (RGBA)
);
```

### 采样器设置

```cpp
// 设置默认采样器参数
textureLoader->setDefaultSamplerParameters(
    VK_FILTER_LINEAR,                    // 线性过滤
    VK_SAMPLER_ADDRESS_MODE_REPEAT,      // 重复模式
    true,                                // 启用各向异性
    16.0f                                // 最大各向异性级别
);

// 禁用 Mipmap 生成
textureLoader->setGenerateMipmaps(false);
```

---

## 纹理格式

### 常用格式

| 格式 | 描述 |
|------|------|
| `VK_FORMAT_R8G8B8A8_SRGB` | RGBA，sRGB 色彩空间 |
| `VK_FORMAT_R8G8B8A8_UNORM` | RGBA，线性色彩空间 |
| `VK_FORMAT_R8G8B8_SRGB` | RGB，sRGB 色彩空间 |
| `VK_FORMAT_R8_UNORM` | 单通道（如高度图） |

### 通道数对应

| 通道数 | 格式 |
|--------|------|
| 1 | R8_UNORM |
| 2 | R8G8_UNORM |
| 3 | R8G8B8_SRGB |
| 4 | R8G8B8A8_SRGB |

---

## 过滤模式

### VkFilter

| 模式 | 描述 |
|------|------|
| `VK_FILTER_NEAREST` | 最近邻过滤（像素化效果） |
| `VK_FILTER_LINEAR` | 线性过滤（平滑效果） |

### VkSamplerAddressMode

| 模式 | 描述 |
|------|------|
| `VK_SAMPLER_ADDRESS_MODE_REPEAT` | 重复纹理 |
| `VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT` | 镜像重复 |
| `VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE` | 边缘拉伸 |
| `VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER` | 边框颜色 |

---

## Mipmap

Mipmap 是预计算的一系列缩小版本的纹理，用于提高渲染质量和性能。

### 自动生成

```cpp
// 启用 Mipmap 生成（默认）
textureLoader->setGenerateMipmaps(true);

// 加载纹理时会自动生成 mipmap
auto texture = textureLoader->loadTexture("texture.png");
```

### 手动计算 MipLevels

```cpp
uint32_t calculateMipLevels(uint32_t width, uint32_t height) {
    return static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;
}
```

---

## 与 glTFModel 集成

```cpp
// 创建纹理加载器
auto textureLoader = std::make_shared<TextureLoader>(device);

// 创建 glTF 模型（使用相同的纹理加载器）
auto model = std::make_unique<GLTFModel>(device, textureLoader);

// 加载模型（自动加载嵌入的纹理）
model->loadFromFile("assets/models/player.glb");
```
