#ifndef ECS_H
#define ECS_H

// ECS 模块主头文件
// 包含共享 ECS 和客户端 ECS 的所有头文件

// 共享 ECS（shared library）
#include "ecs/components.h"
#include "ecs/systems.h"

// 客户端 ECS（client library）
#include "ecs/client_components.h"
#include "ecs/client_systems.h"

#endif // ECS_H