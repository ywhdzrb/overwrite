cmake_minimum_required(VERSION 3.20)
cmake_policy(VERSION 3.20...3.30)
project(OverWrite VERSION 1.0.0 LANGUAGES CXX)

# C++ 标准
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 构建类型
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# 编译选项
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-g -O0 -Wall -Wextra -Wpedantic)
else()
    add_compile_options(-O3 -march=native)
endif()

# ==================== 依赖管理 (统一使用 FetchContent) ====================
include(FetchContent)

# EnTT - ECS 库
FetchContent_Declare(
    entt
    GIT_REPOSITORY https://github.com/skypjack/entt.git
    GIT_TAG v3.13.2
)

# nlohmann_json - JSON 库
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)

# websocketpp - WebSocket 库
FetchContent_Declare(
    websocketpp
    GIT_REPOSITORY https://github.com/zaphoyd/websocketpp.git
    GIT_TAG 0.8.2
)

# ImGui - UI 库
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.8
)

# tinygltf - GLTF 模型加载
FetchContent_Declare(
    tinygltf
    GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
    GIT_TAG v2.9.3
)

# tinyobjloader - OBJ 模型加载
FetchContent_Declare(
    tinyobjloader
    GIT_REPOSITORY https://github.com/tinyobjloader/tinyobjloader.git
    GIT_TAG v2.0.0rc13
)

FetchContent_MakeAvailable(entt nlohmann_json websocketpp imgui tinygltf tinyobjloader)

# ==================== 系统库 ====================
find_package(Vulkan REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(glfw3 REQUIRED)
find_package(glm REQUIRED)

# ==================== ImGui 配置 ====================
set(IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
)

# stb_image.h 由 tinygltf 提供

# ==================== 共享库 (shared) ====================
add_library(OverWriteShared STATIC
    shared/src/ecs/components.cpp
    shared/src/ecs/systems.cpp
    shared/src/network/server_discovery.cpp
)

target_include_directories(OverWriteShared PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/shared/include
)

target_link_libraries(OverWriteShared PUBLIC
    glm::glm
    EnTT::EnTT
    nlohmann_json::nlohmann_json
)

# ==================== 客户端库 (client) ====================
add_library(OverWriteClient STATIC
    client/src/ecs/client_systems.cpp
    client/src/network/network_system.cpp
)

target_include_directories(OverWriteClient PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/client/include
    ${CMAKE_CURRENT_SOURCE_DIR}/shared/include
)

target_link_libraries(OverWriteClient PUBLIC
    OverWriteShared
    glm::glm
    glfw
    nlohmann_json::nlohmann_json
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
)

# ==================== 服务器库 (server) ====================
add_library(OverWriteServer STATIC
    server/src/server/server_world.cpp
    server/src/server/websocket_server.cpp
)

target_include_directories(OverWriteServer PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/server/include
    ${CMAKE_CURRENT_SOURCE_DIR}/shared/include
)

target_include_directories(OverWriteServer PUBLIC
    /usr/include
)

target_link_libraries(OverWriteServer PUBLIC
    OverWriteShared
    nlohmann_json::nlohmann_json
    Threads::Threads
)

# ==================== 客户端主程序 ====================
# 包含目录
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/shared/include
    ${CMAKE_CURRENT_SOURCE_DIR}/client/include
    ${tinygltf_SOURCE_DIR}
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

# 源文件
set(SOURCES
    src/main.cpp
    src/core/vulkan_instance.cpp
    src/core/vulkan_device.cpp
    src/core/vulkan_swapchain.cpp
    src/core/vulkan_render_pass.cpp
    src/core/vulkan_pipeline.cpp
    src/core/vulkan_framebuffer.cpp
    src/core/vulkan_command_buffer.cpp
    src/core/vulkan_sync.cpp
    src/core/camera.cpp
    src/core/input.cpp
    src/core/physics.cpp
    src/core/light.cpp
    src/core/light_manager.cpp
    src/renderer/renderer.cpp
    src/renderer/model.cpp
    src/renderer/mesh.cpp
    src/renderer/model_renderer.cpp
    src/renderer/floor_renderer.cpp
    src/renderer/cube_renderer.cpp
    src/renderer/skybox_renderer.cpp
    src/renderer/imgui_manager.cpp
    src/renderer/texture.cpp
    src/renderer/texture_loader.cpp
    src/renderer/gltf_model.cpp
    src/utils/shader_compiler.cpp
    src/utils/logger.cpp
    ${tinygltf_SOURCE_DIR}/tiny_gltf.cc
)

# 添加 ImGui 源文件
list(APPEND SOURCES ${IMGUI_SOURCES})

# 创建客户端可执行文件
add_executable(${PROJECT_NAME} ${SOURCES})

target_link_libraries(${PROJECT_NAME}
    OverWriteShared
    OverWriteClient
    Vulkan::Vulkan
    glfw
    glm::glm
    EnTT::EnTT
)

# ==================== 服务器主程序 ====================
add_executable(OverWriteServerBin
    server/src/server_main.cpp
)

target_link_libraries(OverWriteServerBin
    OverWriteServer
    nlohmann_json::nlohmann_json
)

set_target_properties(OverWriteServerBin PROPERTIES
    OUTPUT_NAME "overwrite-server"
)

# 着色器编译规则
set(SHADER_SOURCES
    shaders/shader.vert
    shaders/shader.frag
    shaders/skybox.vert
    shaders/skybox.frag
)

foreach(shader_file ${SHADER_SOURCES})
    get_filename_component(shader_name ${shader_file} NAME_WE)
    get_filename_component(shader_ext ${shader_file} EXT)
    
    if(shader_ext STREQUAL ".vert")
        set(shader_type vert)
    elseif(shader_ext STREQUAL ".frag")
        set(shader_type frag)
    else()
        message(FATAL_ERROR "Unknown shader extension: ${shader_ext}")
    endif()
    
    set(output_file "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader_name}${shader_ext}.spv")
    
    add_custom_command(
        OUTPUT ${output_file}
        COMMAND glslc ${CMAKE_CURRENT_SOURCE_DIR}/${shader_file} -o ${output_file}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${shader_file}
        COMMENT "Compiling shader ${shader_file}"
    )
    
    list(APPEND SHADER_OUTPUTS ${output_file})
endforeach()

# 将着色器添加到依赖
add_custom_target(compile_shaders DEPENDS ${SHADER_OUTPUTS})
add_dependencies(${PROJECT_NAME} compile_shaders)

# 安装规则
install(TARGETS ${PROJECT_NAME} OverWriteServerBin DESTINATION bin)

# 输出构建信息
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Vulkan headers: ${Vulkan_INCLUDE_DIRS}")
message(STATUS "Targets: ${PROJECT_NAME} (client), overwrite-server (server)")
