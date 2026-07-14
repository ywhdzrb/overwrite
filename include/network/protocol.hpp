#pragma once

/**
 * @file protocol.hpp
 * @brief 网络协议定义 — 消息类型枚举 + 二进制输入消息格式
 *
 * 归属模块：shared/network
 * 核心职责：统一客户端/服务端间的消息协议，消除硬编码字符串类型
 * 关键设计：高频消息（input）使用固定大小二进制格式以降低开销；
 *           低频消息（welcome/state/join/leave）仍用 JSON 但带上整型类型字段
 */

#include <cstdint>
#include <cstring>
#include <string>

namespace owengine {
namespace network {

// ==================== 消息类型枚举 ====================

/**
 * @brief 消息类型枚举（替代字符串 "type" 字段）
 *
 * 所有 wire 消息均携带此枚举值。二进制消息直接写在首字节，
 * JSON 消息写入 "t" 字段（如 {"t":1, "type":"input", ...}），
 * 接收方优先读取整型 t，回退到字符串 type 保证向后兼容。
 */
enum class MessageType : uint8_t {
    MsgUnknown     = 0,
    MsgInput       = 1,   // C→S 输入同步
    MsgPing        = 2,   // C→S 心跳
    MsgPong        = 3,   // S→C 心跳回声
    MsgWelcome     = 4,   // S→C 欢迎 + 客户端ID + 现有玩家
    MsgPlayerJoin  = 5,   // S→C 新玩家加入
    MsgPlayerLeave = 6,   // S→C 玩家离开
    MsgState       = 7,   // S→C 状态广播
};

// ==================== 二进制输入消息（32 字节） ====================

/**
 * @brief 输入同步消息的二进制格式
 *
 * 替代原有的 ~250 字节 JSON 消息，固定 32 字节。
 * 每帧发送，是网络协议中最高频的消息类型。
 */
struct InputMessage {
    uint8_t type;            // = MessageType::MsgInput
    uint8_t buttons;         // 位域: bit0=forward, bit1=backward, bit2=left, bit3=right,
                             //       bit4=jump, bit5=sprint, bit6=spaceHeld, bit7=shiftHeld
    int16_t mouseDeltaX;     // 量化: 2π/65536 ≈ 0.000096 rad/步
    int16_t mouseDeltaY;
    float cameraFront[3];    // 相机前方方向（归一化）
    float cameraRight[3];    // 相机右方方向（归一化）
};

static_assert(sizeof(InputMessage) == 32, "InputMessage must be exactly 32 bytes");

// ==================== 辅助函数 ====================

/**
 * @brief 将 8 个布尔值打包为 1 字节位域
 */
inline uint8_t packButtons(bool forward, bool backward, bool left, bool right,
                           bool jump, bool sprint, bool spaceHeld, bool shiftHeld) noexcept {
    return (forward  ? 1u : 0u) << 0 |
           (backward ? 1u : 0u) << 1 |
           (left     ? 1u : 0u) << 2 |
           (right    ? 1u : 0u) << 3 |
           (jump     ? 1u : 0u) << 4 |
           (sprint   ? 1u : 0u) << 5 |
           (spaceHeld? 1u : 0u) << 6 |
           (shiftHeld? 1u : 0u) << 7;
}

/**
 * @brief 从 1 字节位域解包 8 个布尔值
 */
inline void unpackButtons(uint8_t buttons, bool& forward, bool& backward,
                          bool& left, bool& right,
                          bool& jump, bool& sprint,
                          bool& spaceHeld, bool& shiftHeld) noexcept {
    forward   = (buttons >> 0) & 1;
    backward  = (buttons >> 1) & 1;
    left      = (buttons >> 2) & 1;
    right     = (buttons >> 3) & 1;
    jump      = (buttons >> 4) & 1;
    sprint    = (buttons >> 5) & 1;
    spaceHeld = (buttons >> 6) & 1;
    shiftHeld = (buttons >> 7) & 1;
}

/**
 * @brief 将 InputMessage 打包为可用于 wire 传输的字符串
 *
 * 使用 std::string 构造（非 memcpy + resize）避免未初始化内存
 */
inline std::string packInputMessage(const InputMessage& msg) noexcept {
    return std::string(reinterpret_cast<const char*>(&msg), sizeof(msg));
}

/**
 * @brief 从 wire 数据解析 InputMessage
 * @return true 解析成功, false 数据无效（大小不足或类型不匹配）
 */
inline bool unpackInputMessage(const std::string& data, InputMessage& msg) noexcept {
    if (data.size() < sizeof(InputMessage)) return false;
    if (static_cast<uint8_t>(data[0]) != static_cast<uint8_t>(MessageType::MsgInput)) return false;
    std::memcpy(&msg, data.data(), sizeof(InputMessage));
    return true;
}

/**
 * @brief 消息类型 → 可读名称（用于日志）
 */
inline const char* messageTypeName(MessageType type) noexcept {
    switch (type) {
        case MessageType::MsgInput:        return "input";
        case MessageType::MsgPing:         return "ping";
        case MessageType::MsgPong:         return "pong";
        case MessageType::MsgWelcome:      return "welcome";
        case MessageType::MsgPlayerJoin:   return "playerJoin";
        case MessageType::MsgPlayerLeave:  return "playerLeave";
        case MessageType::MsgState:        return "state";
        default:               return "unknown";
    }
}

/**
 * @brief 字符串类型名 → 枚举值（JSON 向后兼容用）
 */
inline MessageType messageTypeFromName(const std::string& name) noexcept {
    if (name == "input")        return MessageType::MsgInput;
    if (name == "ping")         return MessageType::MsgPing;
    if (name == "pong")         return MessageType::MsgPong;
    if (name == "welcome")      return MessageType::MsgWelcome;
    if (name == "playerJoin")   return MessageType::MsgPlayerJoin;
    if (name == "playerLeave")  return MessageType::MsgPlayerLeave;
    if (name == "state")        return MessageType::MsgState;
    return MessageType::MsgUnknown;
}

} // namespace network
} // namespace owengine
