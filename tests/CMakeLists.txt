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
