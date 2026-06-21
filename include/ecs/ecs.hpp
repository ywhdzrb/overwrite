#pragma once

// ECS 模块主头文件
// 包含所有共享 ECS 接口，客户端专用头文件需单独包含

// 共享 ECS（shared library）
#include "ecs/components.hpp"
#include "ecs/resource_types.hpp"
#include "ecs/systems.hpp"

// 统一实体工厂（shared layer）
#include "ecs/entity_factory.hpp"

// 高级实体接口（renderer layer）
#include "ecs/i_game_entity.hpp"

// 集中式注册表（renderer layer）
#include "ecs/entity_registry.hpp"
