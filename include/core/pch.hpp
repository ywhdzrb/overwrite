#pragma once

/**
 * @file pch.hpp
 * @brief 预编译头文件 — 将稳定且频繁使用的基础头文件提前编译以加速构建
 *
 * 包含：标准库容器/智能指针/原子操作、Vulkan/GLM/GLFW 等第三方库头文件。
 * 新增稳定依赖时，若被 10+ 个翻译单元使用，应考虑加到此文件。
 */

// 标准库
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>

// 第三方库
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
