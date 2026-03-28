cmake_minimum_required(VERSION 3.20)
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

# 查找 Vulkan
find_package(Vulkan REQUIRED)
include_directories(${Vulkan_INCLUDE_DIRS})

# 查找 GLFW (用于窗口管理)
find_package(glfw3 REQUIRED)

# 查找 glm (数学库)
find_package(glm REQUIRED)

# 使用本地 git submodule 的 tinyobjloader
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/external/tinyobjloader)

# 包含目录
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
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
    src/renderer/renderer.cpp
    src/renderer/model.cpp
    src/renderer/mesh.cpp
    src/utils/shader_compiler.cpp
    src/utils/logger.cpp
)

# 创建可执行文件
add_executable(${PROJECT_NAME} ${SOURCES})

# 链接库
target_link_libraries(${PROJECT_NAME}
    Vulkan::Vulkan
    glfw
    glm::glm
)

# 着色器编译规则
function(compile_shader shader_file)
    get_filename_component(shader_name ${shader_file} NAME_WE)
    get_filename_component(shader_ext ${shader_file} EXT)
    
    if(shader_ext STREQUAL ".vert")
        set(shader_type vert)
    elseif(shader_ext STREQUAL ".frag")
        set(shader_type frag)
    else()
        message(FATAL_ERROR "Unknown shader extension: ${shader_ext}")
    endif()
    
    set(output_file "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader_name}.spv")
    
    add_custom_command(
        OUTPUT ${output_file}
        COMMAND glslc ${CMAKE_CURRENT_SOURCE_DIR}/${shader_file} -o ${output_file}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${shader_file}
        COMMENT "Compiling shader ${shader_file}"
    )
    
    list(APPEND SHADER_OUTPUTS ${output_file})
endfunction()

# 编译着色器
file(GLOB SHADER_FILES "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.vert" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.frag")
foreach(shader ${SHADER_FILES})
    get_filename_component(shader_name ${shader} NAME)
    compile_shader(${shader_name})
endforeach()

# 将着色器添加到依赖
add_custom_target(compile_shaders DEPENDS ${SHADER_OUTPUTS})
add_dependencies(${PROJECT_NAME} compile_shaders)

# 安装规则
install(TARGETS ${PROJECT_NAME} DESTINATION bin)

# 输出构建信息
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Vulkan headers: ${Vulkan_INCLUDE_DIRS}")