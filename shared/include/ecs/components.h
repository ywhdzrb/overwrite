#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <cstdint>

namespace owengine {
namespace ecs {

/**
 * @brief 变换组件 - 位置、旋转、缩放（共享）
 */
struct TransformComponent {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};  // w, x, y, z
    glm::vec3 scale{1.0f};
    
    // 欧拉角（用于相机等需要欧拉角控制的场景）
    float yaw{0.0f};
    float pitch{0.0f};
    float roll{0.0f};
    
    // 获取前向量（基于欧拉角）
    glm::vec3 getFront() const;
    glm::vec3 getRight() const;
    glm::vec3 getUp() const;
    
    // 从欧拉角更新四元数
    void updateRotationFromEuler();
    
    // 获取模型矩阵
    glm::mat4 getModelMatrix() const;
};

/**
 * @brief 速度组件（共享）
 */
struct VelocityComponent {
    glm::vec3 linear{0.0f};
    glm::vec3 angular{0.0f};
};

/**
 * @brief 物理组件（共享）
 * 
 * 地形系统设计说明：
 * - groundHeight: 当前站立面的高度（动态更新，由地形系统/碰撞检测设置）
 * - isGrounded: 是否着地（核心状态，决定是否应用重力）
 * - 跳跃判定：仅在 isGrounded=true 时允许跳跃
 */
struct PhysicsComponent {
    float gravity{15.0f};
    float groundHeight{-1.5f};      // 当前站立面高度（由地形系统动态更新）
    glm::vec3 groundNormal{0.0f, 1.0f, 0.0f};  // 地面法向量（由地形系统计算）
    float jumpForce{5.5f};
    
    bool isJumping{false};          // 是否正在跳跃（上升阶段）
    bool isGrounded{true};          // 是否着地（核心状态）
    bool useGravity{true};
    
    // 碰撞体参数
    float colliderHeight{1.8f};
    float colliderRadius{0.3f};
    
    // 地形查询缓存（由地形系统填充）
    float cachedTerrainHeight{-1.5f};  // 缓存的地形高度
    bool terrainCacheValid{false};     // 缓存是否有效
};

/**
 * @brief 输入组件 - 存储输入状态（共享）
 * 服务器从网络接收，客户端从本地输入
 */
struct InputStateComponent {
    // 移动输入
    bool moveForward{false};
    bool moveBackward{false};
    bool moveLeft{false};
    bool moveRight{false};
    bool jump{false};
    bool sprint{false};
    bool freeCameraToggle{false};
    bool spaceHeld{false};
    bool shiftHeld{false};
    
    // 鼠标输入
    float mouseDeltaX{0.0f};
    float mouseDeltaY{0.0f};
    
    // 重置输入状态
    void reset() {
        moveForward = moveBackward = moveLeft = moveRight = false;
        jump = sprint = freeCameraToggle = false;
        spaceHeld = shiftHeld = false;
        mouseDeltaX = mouseDeltaY = 0.0f;
    }
};

/**
 * @brief 玩家标签组件（共享）
 */
struct PlayerTag {
    uint32_t playerId{0};
    uint32_t connectionId{0};  // 用于服务器关联连接
};

/**
 * @brief 名称组件（共享）
 */
struct NameComponent {
    std::string name;
};

/**
 * @brief 移动控制器组件（共享）
 */
struct MovementControllerComponent {
    float movementSpeed{5.0f};
    float sprintMultiplier{2.0f};
    float mouseSensitivity{0.1f};
    
    // 第三人称模式下的移动方向（由相机同步）
    glm::vec3 moveFront{0.0f, 0.0f, -1.0f};
    glm::vec3 moveRight{1.0f, 0.0f, 0.0f};
    
    // 空中控制参数
    float airControlFactor{0.2f};  // 空中移动控制系数 (0.0-1.0)
};

/**
 * @brief 网络同步组件（共享）
 */
struct NetworkSyncComponent {
    uint32_t networkId{0};         // 网络唯一ID
    uint32_t lastSyncFrame{0};     // 上次同步帧
    bool needsSync{true};          // 是否需要同步
    bool isOwned{false};           // 是否被本地玩家拥有
};

/**
 * @brief 实体类型枚举（共享）
 */
enum class EntityType : uint8_t {
    Unknown = 0,
    Player = 1,
    NPC = 2,
    Building = 3,
    Item = 4,
    Projectile = 5,
};

/**
 * @brief 实体类型组件（共享）
 */
struct EntityTypeComponent {
    EntityType type{EntityType::Unknown};
};

} // namespace ecs
} // namespace owengine
