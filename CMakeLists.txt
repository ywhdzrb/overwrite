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

# ==================== 外部依赖路径 ====================
# 外部依赖由 git submodules 管理，通过 build.sh 初始化
# 运行 ./build.sh 会自动 checkout 到指定版本
set(EXTERNAL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external)

# ==================== 系统库 ====================
find_package(Vulkan REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(glfw3 REQUIRED)
find_package(glm REQUIRED)
find_package(ZLIB REQUIRED)

# ==================== EnTT (header-only) ====================
add_library(EnTT INTERFACE)
target_include_directories(EnTT INTERFACE ${EXTERNAL_DIR}/entt/single_include)

# ==================== nlohmann_json (header-only) ====================
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE ${EXTERNAL_DIR}/nlohmann_json/include)

# ==================== ixwebsocket ====================
set(IXWEBSOCKET_DIR ${EXTERNAL_DIR}/ixwebsocket/ixwebsocket)
set(IXWEBSOCKET_SOURCES
    ${IXWEBSOCKET_DIR}/IXWebSocket.cpp
    ${IXWEBSOCKET_DIR}/IXWebSocketServer.cpp
    ${IXWEBSOCKET_DIR}/IXWebSocketTransport.cpp
    ${IXWEBSOCKET_DIR}/IXWebSocketHttpHeaders.cpp
    ${IXWEBSOCKET_DIR}/IXWebSocketHandshake.cpp
    ${IXWEBSOCKET_DIR}/IXWebSocketCloseConstants.cpp
    ${IXWEBSOCKET_DIR}/IXWebSocketPerMessageDeflate.cpp
    ${IXWEBSOCKET_DIR}/IXWebSocketPerMessageDeflateCodec.cpp
    ${IXWEBSOCKET_DIR}/IXWebSocketPerMessageDeflateOptions.cpp
    ${IXWEBSOCKET_DIR}/IXWebSocketProxyServer.cpp
    ${IXWEBSOCKET_DIR}/IXSocket.cpp
    ${IXWEBSOCKET_DIR}/IXSocketConnect.cpp
    ${IXWEBSOCKET_DIR}/IXSocketFactory.cpp
    ${IXWEBSOCKET_DIR}/IXSocketOpenSSL.cpp
    ${IXWEBSOCKET_DIR}/IXSocketServer.cpp
    ${IXWEBSOCKET_DIR}/IXSocketTLSOptions.cpp
    ${IXWEBSOCKET_DIR}/IXUdpSocket.cpp
    ${IXWEBSOCKET_DIR}/IXNetSystem.cpp
    ${IXWEBSOCKET_DIR}/IXDNSLookup.cpp
    ${IXWEBSOCKET_DIR}/IXUuid.cpp
    ${IXWEBSOCKET_DIR}/IXSelectInterrupt.cpp
    ${IXWEBSOCKET_DIR}/IXSelectInterruptPipe.cpp
    ${IXWEBSOCKET_DIR}/IXSelectInterruptEvent.cpp
    ${IXWEBSOCKET_DIR}/IXSelectInterruptFactory.cpp
    ${IXWEBSOCKET_DIR}/IXStrCaseCompare.cpp
    ${IXWEBSOCKET_DIR}/IXHttp.cpp
    ${IXWEBSOCKET_DIR}/IXHttpClient.cpp
    ${IXWEBSOCKET_DIR}/IXHttpServer.cpp
    ${IXWEBSOCKET_DIR}/IXUrlParser.cpp
    ${IXWEBSOCKET_DIR}/IXUserAgent.cpp
    ${IXWEBSOCKET_DIR}/IXConnectionState.cpp
    ${IXWEBSOCKET_DIR}/IXCancellationRequest.cpp
    ${IXWEBSOCKET_DIR}/IXExponentialBackoff.cpp
    ${IXWEBSOCKET_DIR}/IXSetThreadName.cpp
    ${IXWEBSOCKET_DIR}/IXGzipCodec.cpp
    ${IXWEBSOCKET_DIR}/IXGetFreePort.cpp
)

add_library(ixwebsocket STATIC ${IXWEBSOCKET_SOURCES})
target_include_directories(ixwebsocket PUBLIC ${EXTERNAL_DIR}/ixwebsocket)
target_link_libraries(ixwebsocket PUBLIC OpenSSL::SSL OpenSSL::Crypto Threads::Threads ZLIB::ZLIB)
target_compile_definitions(ixwebsocket PUBLIC IXWEBSOCKET_USE_OPEN_SSL IXWEBSOCKET_USE_ZLIB)

# ==================== ImGui ====================
set(IMGUI_SOURCES
    ${EXTERNAL_DIR}/imgui/imgui.cpp
    ${EXTERNAL_DIR}/imgui/imgui_draw.cpp
    ${EXTERNAL_DIR}/imgui/imgui_widgets.cpp
    ${EXTERNAL_DIR}/imgui/imgui_tables.cpp
    ${EXTERNAL_DIR}/imgui/backends/imgui_impl_vulkan.cpp
    ${EXTERNAL_DIR}/imgui/backends/imgui_impl_glfw.cpp
)

add_library(imgui STATIC ${IMGUI_SOURCES})
target_include_directories(imgui PUBLIC 
    ${EXTERNAL_DIR}/imgui
    ${EXTERNAL_DIR}/imgui/backends
)
target_link_libraries(imgui PUBLIC glfw Vulkan::Vulkan)

# ==================== tinygltf (header-only, with stb_image) ====================
set(STB_SOURCES
    ${EXTERNAL_DIR}/tinygltf/tiny_gltf.cc
)
add_library(tinygltf STATIC ${STB_SOURCES})
target_include_directories(tinygltf PUBLIC ${EXTERNAL_DIR}/tinygltf)

# ==================== tinyobjloader (header-only with implementation) ====================
set(TINYOBJ_SOURCES
    ${EXTERNAL_DIR}/tinyobjloader/tiny_obj_loader.cc
)
add_library(tinyobjloader STATIC ${TINYOBJ_SOURCES})
target_include_directories(tinyobjloader PUBLIC ${EXTERNAL_DIR}/tinyobjloader)

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
    EnTT
    nlohmann_json
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
    nlohmann_json
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

target_link_libraries(OverWriteServer PUBLIC
    OverWriteShared
    ixwebsocket
    nlohmann_json
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
)

# ==================== 客户端主程序 ====================
# 包含目录
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/shared/include
    ${CMAKE_CURRENT_SOURCE_DIR}/client/include
    ${EXTERNAL_DIR}/tinygltf
    ${EXTERNAL_DIR}/imgui
    ${EXTERNAL_DIR}/imgui/backends
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
)

# 创建客户端可执行文件
add_executable(${PROJECT_NAME} ${SOURCES})

target_link_libraries(${PROJECT_NAME}
    OverWriteShared
    OverWriteClient
    Vulkan::Vulkan
    glfw
    glm::glm
    EnTT
    imgui
    tinygltf
    tinyobjloader
)

# ==================== 服务器主程序 ====================
add_executable(OverWriteServerBin
    server/src/server_main.cpp
)

target_link_libraries(OverWriteServerBin
    OverWriteServer
    nlohmann_json
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