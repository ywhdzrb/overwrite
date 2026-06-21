#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <cstdint>

namespace owengine {
namespace ecs {

// ========== 游戏全局常量 ==========
inline constexpr float MAX_DELTA_TIME   = 0.1f;    // 最大帧间隔（秒），超过此值会被钳制
inline constexpr float PLAYER_RADIUS    = 0.3f;    // 玩家碰撞体半径
inline constexpr float PLAYER_HEIGHT    = 1.8f;    // 玩家碰撞体高度
inline constexpr float PLAYER_MODEL_SCALE = 0.3f;  // 玩家模型的缩放系数

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
    Vehicle = 6,
    Animal = 7,
    Weapon = 8,
    Pickup = 9,
    Door = 10,
    Chest = 11,
    Foliage = 12,
    Water = 13,
    Explosive = 14,
    SoundSource = 15,
    Zone = 16,
};

/**
 * @brief 实体类型组件（共享）
 */
struct EntityTypeComponent {
    EntityType type{EntityType::Unknown};
};

/**
 * @brief 血量组件
 * @note 适用于所有可受伤/可破坏实体（Player、NPC、Animal、Explosive 等）
 */
struct HealthComponent {
    float current{100.0f};
    float max{100.0f};
    bool invincible{false};
    bool isDead{false};

    /** @brief 受到伤害，返回实际扣血量 */
    float takeDamage(float amount) {
        if (invincible || isDead) return 0.0f;
        float actual = std::min(amount, current);
        current -= actual;
        if (current <= 0.0f) {
            current = 0.0f;
            isDead = true;
        }
        return actual;
    }

    /** @brief 恢复血量，返回实际回复量 */
    float heal(float amount) {
        if (isDead) return 0.0f;
        float before = current;
        current = std::min(current + amount, max);
        return current - before;
    }

    /** @brief 重置为满血 */
    void reset() {
        current = max;
        isDead = false;
    }
};

/**
 * @brief 交互组件
 * @note 适用于可交互实体（Door、Chest、Pickup、NPC 等）
 */
struct InteractionComponent {
    bool interactable{true};
    float interactRange{2.0f};          // 触发交互的最大距离
    std::string prompt{"Press E to interact"};
    bool highlightOnHover{true};

    // 冷却时间（秒），防止连续触发
    float cooldown{0.0f};
    float lastInteractTime{0.0f};

    /** @brief 是否可以再次交互 */
    bool canInteract(float currentTime) const {
        if (!interactable) return false;
        return (currentTime - lastInteractTime) >= cooldown;
    }

    /** @brief 标记交互时间 */
    void markInteracted(float currentTime) {
        lastInteractTime = currentTime;
    }
};

/**
 * @brief 载具组件
 * @note 适用于 Vehicle 类型的实体
 */
struct VehicleComponent {
    float forwardSpeed{15.0f};          // 前进速度
    float reverseSpeed{8.0f};           // 后退速度
    float turnSpeed{90.0f};             // 转向速度（度/秒）
    float acceleration{5.0f};           // 加速度
    float brakingForce{10.0f};          // 制动力

    int passengerCapacity{2};           // 载客量（包含驾驶员）
    int currentPassengers{0};

    bool engineOn{false};
    bool hasCollisionDamage{true};
};

/**
 * @brief 武器组件
 * @note 适用于 Weapon 类型的实体
 */
struct WeaponComponent {
    enum class Category {
        Melee,
        Ranged,
        Thrown,
        Magic,
    };

    Category category{Category::Melee};
    float damage{10.0f};
    float range{2.0f};                  // 近战范围/远程有效距离
    float fireRate{1.0f};              // 攻击频率（次/秒）
    float reloadTime{2.0f};            // 装填时间（秒）

    int maxAmmo{30};
    int currentAmmo{30};
    bool autoFire{false};               // 是否自动连续开火

    float lastFireTime{0.0f};

    /** @brief 是否可以开火 */
    bool canFire(float currentTime) const {
        if (currentAmmo <= 0) return false;
        return (currentTime - lastFireTime) >= (1.0f / fireRate);
    }

    /** @brief 开火消耗弹药 */
    void fire() {
        if (currentAmmo > 0) currentAmmo--;
        lastFireTime = 0.0f; // 由外部传入实际时间
    }

    /** @brief 装填 */
    void reload() {
        currentAmmo = maxAmmo;
    }
};

/**
 * @brief 植被组件
 * @note 适用于 Foliage 类型的实体，控制风动效果和 LOD
 */
struct FoliageComponent {
    float windInfluence{1.0f};          // 风影响系数（0 = 不受风影响）
    float swaySpeed{1.0f};              // 摆动速度
    float swayAmplitude{0.1f};          // 摆动幅度
    bool hasLOD{true};                  // 是否启用 LOD
    float cutRadius{0.0f};             // 被砍伐后的缺口半径（0 = 未砍伐）
};

/**
 * @brief 水体组件
 * @note 适用于 Water 类型的实体
 */
struct WaterComponent {
    enum class Preset {
        Calm,
        Ripple,
        Waves,
        Rapid,
    };

    Preset preset{Preset::Calm};
    float waveHeight{0.5f};
    float waveSpeed{1.0f};
    float waveFrequency{0.1f};
    glm::vec3 flowDirection{0.0f, 0.0f, 0.0f};
    float flowSpeed{0.0f};              // 水流速度
    float depth{5.0f};                  // 水深
    bool transparent{true};
    float opacity{0.6f};
    bool hasReflection{true};
    bool hasRefraction{false};
};

/**
 * @brief 爆炸物组件
 * @note 适用于 Explosive 类型的实体
 */
struct ExplosiveComponent {
    float blastRadius{5.0f};
    float blastDamage{50.0f};
    float fuseTime{0.0f};              // 0 = 即时爆炸，>0 = 延时引爆
    bool armed{true};
    bool hasExploded{false};

    // 物理冲击参数
    float impulseForce{500.0f};
    float debrisCount{10};

    /** @brief 引爆 */
    void detonate() {
        hasExploded = true;
        armed = false;
    }
};

/**
 * @brief 音源组件
 * @note 适用于 SoundSource 类型的实体
 */
struct SoundSourceComponent {
    std::string soundPath;
    float volume{1.0f};
    float pitch{1.0f};
    float range{20.0f};                 // 可听距离
    float innerRange{5.0f};            // 全音量范围
    bool looping{false};
    bool autoPlay{false};
    bool spatialAudio{true};           // 是否启用 3D 空间音频

    // 状态
    bool isPlaying{false};
    bool isPaused{false};
};

} // namespace ecs
} // namespace owengine
