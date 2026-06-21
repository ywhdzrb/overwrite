#pragma once

/**
 * @file vk_result.hpp
 * @brief Vulkan 错误码转可读字符串，用于 throw/log 信息增强
 *
 * 归属模块：utils
 * 核心职责：将 VkResult 枚举值转为中文/英文可读字符串
 * 使用场景：所有 Vulkan API 调用失败时，将错误码拼接至异常消息
 */

#include <string>
#include <vulkan/vulkan.h>

namespace owengine {

/**
 * @brief VkResult → 可读字符串
 * @param result Vulkan API 返回的错误码
 * @return 形如 "VK_ERROR_OUT_OF_HOST_MEMORY (-1)" 的字符串
 *
 * 覆盖 Vulkan 1.3 所有标准错误码，未知值回退为 "UNKNOWN(<code>)"。
 * 线程安全：纯查询函数，无状态。
 */
[[nodiscard]] inline std::string vkResultToString(VkResult result) {
    switch (result) {
        case VK_SUCCESS:                       return "VK_SUCCESS (0)";
        case VK_NOT_READY:                     return "VK_NOT_READY (1)";
        case VK_TIMEOUT:                       return "VK_TIMEOUT (2)";
        case VK_EVENT_SET:                     return "VK_EVENT_SET (3)";
        case VK_EVENT_RESET:                   return "VK_EVENT_RESET (4)";
        case VK_INCOMPLETE:                    return "VK_INCOMPLETE (5)";
        case VK_ERROR_OUT_OF_HOST_MEMORY:      return "VK_ERROR_OUT_OF_HOST_MEMORY (-1)";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY:    return "VK_ERROR_OUT_OF_DEVICE_MEMORY (-2)";
        case VK_ERROR_INITIALIZATION_FAILED:   return "VK_ERROR_INITIALIZATION_FAILED (-3)";
        case VK_ERROR_DEVICE_LOST:             return "VK_ERROR_DEVICE_LOST (-4)";
        case VK_ERROR_MEMORY_MAP_FAILED:       return "VK_ERROR_MEMORY_MAP_FAILED (-5)";
        case VK_ERROR_LAYER_NOT_PRESENT:       return "VK_ERROR_LAYER_NOT_PRESENT (-6)";
        case VK_ERROR_EXTENSION_NOT_PRESENT:   return "VK_ERROR_EXTENSION_NOT_PRESENT (-7)";
        case VK_ERROR_FEATURE_NOT_PRESENT:     return "VK_ERROR_FEATURE_NOT_PRESENT (-8)";
        case VK_ERROR_INCOMPATIBLE_DRIVER:     return "VK_ERROR_INCOMPATIBLE_DRIVER (-9)";
        case VK_ERROR_TOO_MANY_OBJECTS:        return "VK_ERROR_TOO_MANY_OBJECTS (-10)";
        case VK_ERROR_FORMAT_NOT_SUPPORTED:    return "VK_ERROR_FORMAT_NOT_SUPPORTED (-11)";
        case VK_ERROR_FRAGMENTED_POOL:         return "VK_ERROR_FRAGMENTED_POOL (-12)";
        case VK_ERROR_UNKNOWN:                 return "VK_ERROR_UNKNOWN (-13)";
        case VK_ERROR_OUT_OF_POOL_MEMORY:      return "VK_ERROR_OUT_OF_POOL_MEMORY (-1000069000)";
        case VK_ERROR_INVALID_EXTERNAL_HANDLE: return "VK_ERROR_INVALID_EXTERNAL_HANDLE (-1000072003)";
        case VK_ERROR_FRAGMENTATION:           return "VK_ERROR_FRAGMENTATION (-1000161000)";
        case VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS: return "VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS (-1000257000)";
        case VK_PIPELINE_COMPILE_REQUIRED:     return "VK_PIPELINE_COMPILE_REQUIRED (1000297000)";
        case VK_ERROR_SURFACE_LOST_KHR:        return "VK_ERROR_SURFACE_LOST_KHR (-1000000000)";
        case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR (-1000000001)";
        case VK_SUBOPTIMAL_KHR:                return "VK_SUBOPTIMAL_KHR (1000001003)";
        case VK_ERROR_OUT_OF_DATE_KHR:         return "VK_ERROR_OUT_OF_DATE_KHR (-1000001004)";
        case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR:return "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR (-1000003001)";
        case VK_ERROR_VALIDATION_FAILED_EXT:   return "VK_ERROR_VALIDATION_FAILED_EXT (-1000011001)";
        case VK_ERROR_INVALID_SHADER_NV:       return "VK_ERROR_INVALID_SHADER_NV (-1000012000)";
        case VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT: return "VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT (-1000158000)";
        case VK_ERROR_NOT_PERMITTED_EXT:       return "VK_ERROR_NOT_PERMITTED_EXT (-1000174001)";
        case VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT: return "VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT (-1000255000)";
        default:                               return "UNKNOWN (" + std::to_string(static_cast<int>(result)) + ")";
    }
}

} // namespace owengine
