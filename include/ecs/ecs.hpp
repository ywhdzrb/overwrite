#pragma once

/**
 * @file ecs.hpp
 * @brief ECS 模块主头文件 — 聚合共享 ECS 组件/系统/资源类型
 *
 * 归属模块：ecs
 * 核心职责：一键包含所有共享 ECS 接口
 * 关键设计：客户端专用组件需单独包含 client_components.hpp
 */

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
