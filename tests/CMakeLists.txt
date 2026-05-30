# Google Test（FetchContent 自动下载）
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
FetchContent_MakeAvailable(googletest)

# 测试：AssetPaths 常量验证
add_executable(test_asset_paths test_asset_paths.cpp)
target_link_libraries(test_asset_paths GTest::gtest_main)
target_include_directories(test_asset_paths PRIVATE ${CMAKE_SOURCE_DIR}/include)
add_test(NAME AssetPaths COMMAND test_asset_paths)

# 测试：GameConfig JSON 加载
add_executable(test_config test_config.cpp)
target_link_libraries(test_config GTest::gtest_main nlohmann_json)
target_include_directories(test_config PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/external/nlohmann_json/include
)
target_compile_definitions(test_config PRIVATE TEST_CONFIG_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME GameConfig COMMAND test_config)

# 测试：TransformComponent 数学运算（前向量/模型矩阵/四元数）
add_executable(test_transform test_transform.cpp)
target_link_libraries(test_transform GTest::gtest_main OverWriteShared)
target_include_directories(test_transform PRIVATE ${CMAKE_SOURCE_DIR}/include)
add_test(NAME Transform COMMAND test_transform)

# ==================== 新测试 ====================

# 测试：库存系统（ItemStack / InventoryComponent / ResourceData）
# header-only 依赖，不需要链接 OverWriteShared
add_executable(test_inventory test_inventory.cpp)
target_link_libraries(test_inventory GTest::gtest_main)
target_include_directories(test_inventory PRIVATE
    ${CMAKE_SOURCE_DIR}/shared/include
    ${CMAKE_SOURCE_DIR}/include
)
add_test(NAME Inventory COMMAND test_inventory)

# 测试：线程池（ThreadPool）
# header-only 依赖，只需要 GTest 和 Threads
add_executable(test_thread_pool test_thread_pool.cpp)
target_link_libraries(test_thread_pool GTest::gtest_main Threads::Threads)
target_include_directories(test_thread_pool PRIVATE ${CMAKE_SOURCE_DIR}/include)
add_test(NAME ThreadPool COMMAND test_thread_pool)

# 测试：ECS 组件数据默认值
add_executable(test_ecs_components test_ecs_components.cpp)
target_link_libraries(test_ecs_components GTest::gtest_main OverWriteShared)
add_test(NAME ECSComponents COMMAND test_ecs_components)

# 测试：ECS World 实体管理
add_executable(test_ecs_world test_ecs_world.cpp)
target_link_libraries(test_ecs_world GTest::gtest_main OverWriteShared)
add_test(NAME ECSWorld COMMAND test_ecs_world)

# 测试：PhysicsSystem 物理系统（AABB碰撞/重力/地形高度）
add_executable(test_physics_system test_physics_system.cpp)
target_link_libraries(test_physics_system GTest::gtest_main OverWriteShared)
add_test(NAME PhysicsSystem COMMAND test_physics_system)
