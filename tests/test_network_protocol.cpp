// 网络协议测试 — packButtons/unpackButtons/packInputMessage/unpackInputMessage/messageTypeName
#include <gtest/gtest.h>
#include "network/protocol.hpp"

using namespace owengine::network;

// ==================== packButtons / unpackButtons 测试 ====================

TEST(NetworkProtocolTest, PackButtonsAllFalse) {
    uint8_t packed = packButtons(false, false, false, false, false, false, false, false);
    EXPECT_EQ(packed, 0);
}

TEST(NetworkProtocolTest, PackButtonsAllTrue) {
    uint8_t packed = packButtons(true, true, true, true, true, true, true, true);
    EXPECT_EQ(packed, 0xFF);
}

TEST(NetworkProtocolTest, PackButtonsIndividual) {
    EXPECT_EQ(packButtons(true, false, false, false, false, false, false, false), 1u << 0);
    EXPECT_EQ(packButtons(false, true, false, false, false, false, false, false), 1u << 1);
    EXPECT_EQ(packButtons(false, false, true, false, false, false, false, false), 1u << 2);
    EXPECT_EQ(packButtons(false, false, false, true, false, false, false, false), 1u << 3);
    EXPECT_EQ(packButtons(false, false, false, false, true, false, false, false), 1u << 4);
    EXPECT_EQ(packButtons(false, false, false, false, false, true, false, false), 1u << 5);
    EXPECT_EQ(packButtons(false, false, false, false, false, false, true, false), 1u << 6);
    EXPECT_EQ(packButtons(false, false, false, false, false, false, false, true), 1u << 7);
}

TEST(NetworkProtocolTest, PackButtonsMixed) {
    uint8_t packed = packButtons(true, false, true, false, true, false, true, false);
    uint8_t expected = (1u << 0) | (1u << 2) | (1u << 4) | (1u << 6);
    EXPECT_EQ(packed, expected);
}

TEST(NetworkProtocolTest, UnpackButtonsAllFalse) {
    bool f, b, l, r, j, s, sp, sh;
    unpackButtons(0, f, b, l, r, j, s, sp, sh);
    EXPECT_FALSE(f); EXPECT_FALSE(b); EXPECT_FALSE(l); EXPECT_FALSE(r);
    EXPECT_FALSE(j); EXPECT_FALSE(s); EXPECT_FALSE(sp); EXPECT_FALSE(sh);
}

TEST(NetworkProtocolTest, UnpackButtonsAllTrue) {
    bool f, b, l, r, j, s, sp, sh;
    unpackButtons(0xFF, f, b, l, r, j, s, sp, sh);
    EXPECT_TRUE(f); EXPECT_TRUE(b); EXPECT_TRUE(l); EXPECT_TRUE(r);
    EXPECT_TRUE(j); EXPECT_TRUE(s); EXPECT_TRUE(sp); EXPECT_TRUE(sh);
}

TEST(NetworkProtocolTest, UnpackButtonsMixed) {
    bool f, b, l, r, j, s, sp, sh;
    unpackButtons(0b01010101, f, b, l, r, j, s, sp, sh);
    EXPECT_TRUE(f);  EXPECT_FALSE(b); EXPECT_TRUE(l);  EXPECT_FALSE(r);
    EXPECT_TRUE(j);  EXPECT_FALSE(s); EXPECT_TRUE(sp); EXPECT_FALSE(sh);
}

TEST(NetworkProtocolTest, PackUnpackRoundTrip) {
    bool tests[][8] = {
        {true, true, true, true, true, true, true, true},
        {false, false, false, false, false, false, false, false},
        {true, false, true, false, true, false, true, false},
        {false, true, false, true, false, true, false, true},
    };
    for (auto& vals : tests) {
        uint8_t packed = packButtons(vals[0], vals[1], vals[2], vals[3],
                                     vals[4], vals[5], vals[6], vals[7]);
        bool f2, b2, l2, r2, j2, s2, sp2, sh2;
        unpackButtons(packed, f2, b2, l2, r2, j2, s2, sp2, sh2);
        EXPECT_EQ(vals[0], f2); EXPECT_EQ(vals[1], b2);
        EXPECT_EQ(vals[2], l2); EXPECT_EQ(vals[3], r2);
        EXPECT_EQ(vals[4], j2); EXPECT_EQ(vals[5], s2);
        EXPECT_EQ(vals[6], sp2); EXPECT_EQ(vals[7], sh2);
    }
}

// ==================== InputMessage 大小与序列化测试 ====================

TEST(NetworkProtocolTest, InputMessageSize) {
    EXPECT_EQ(sizeof(InputMessage), 32);
}

TEST(NetworkProtocolTest, InputMessageDefaults) {
    InputMessage msg{};
    EXPECT_EQ(msg.type, 0);
    EXPECT_EQ(msg.buttons, 0);
    EXPECT_EQ(msg.mouseDeltaX, 0);
    EXPECT_EQ(msg.mouseDeltaY, 0);
}

TEST(NetworkProtocolTest, PackInputMessageRoundTrip) {
    InputMessage msg;
    msg.type = MSG_INPUT;
    msg.buttons = packButtons(true, false, true, false, false, true, false, false);
    msg.mouseDeltaX = 1234;
    msg.mouseDeltaY = -567;
    msg.cameraFront[0] = 1.0f; msg.cameraFront[1] = 0.0f; msg.cameraFront[2] = 0.0f;
    msg.cameraRight[0] = 0.0f; msg.cameraRight[1] = 0.0f; msg.cameraRight[2] = 1.0f;

    std::string wire = packInputMessage(msg);
    EXPECT_EQ(wire.size(), 32);

    InputMessage msg2{};
    bool ok = unpackInputMessage(wire, msg2);
    EXPECT_TRUE(ok);
    EXPECT_EQ(msg2.type, MSG_INPUT);
    EXPECT_EQ(msg2.buttons, msg.buttons);
    EXPECT_EQ(msg2.mouseDeltaX, 1234);
    EXPECT_EQ(msg2.mouseDeltaY, -567);
    EXPECT_FLOAT_EQ(msg2.cameraFront[0], 1.0f);
    EXPECT_FLOAT_EQ(msg2.cameraFront[2], 0.0f);
    EXPECT_FLOAT_EQ(msg2.cameraRight[2], 1.0f);
}

TEST(NetworkProtocolTest, UnpackInputMessageTooShort) {
    InputMessage msg{};
    EXPECT_FALSE(unpackInputMessage("too short", msg));
}

TEST(NetworkProtocolTest, UnpackInputMessageWrongType) {
    InputMessage msg{};
    // MSG_UNKNOWN (0) 不匹配 MSG_INPUT (1)
    std::string data(32, '\0');
    EXPECT_FALSE(unpackInputMessage(data, msg));
}

TEST(NetworkProtocolTest, UnpackInputMessageEmpty) {
    InputMessage msg{};
    EXPECT_FALSE(unpackInputMessage("", msg));
}

// ==================== messageTypeName 测试 ====================

TEST(NetworkProtocolTest, MessageTypeName) {
    EXPECT_STREQ(messageTypeName(MSG_INPUT),        "input");
    EXPECT_STREQ(messageTypeName(MSG_PING),         "ping");
    EXPECT_STREQ(messageTypeName(MSG_PONG),         "pong");
    EXPECT_STREQ(messageTypeName(MSG_WELCOME),      "welcome");
    EXPECT_STREQ(messageTypeName(MSG_PLAYER_JOIN),  "playerJoin");
    EXPECT_STREQ(messageTypeName(MSG_PLAYER_LEAVE), "playerLeave");
    EXPECT_STREQ(messageTypeName(MSG_STATE),        "state");
    EXPECT_STREQ(messageTypeName(MSG_UNKNOWN),      "unknown");
    EXPECT_STREQ(messageTypeName(static_cast<MessageType>(99)), "unknown");
}

// ==================== messageTypeFromName 测试 ====================

TEST(NetworkProtocolTest, MessageTypeFromName) {
    EXPECT_EQ(messageTypeFromName("input"),        MSG_INPUT);
    EXPECT_EQ(messageTypeFromName("ping"),         MSG_PING);
    EXPECT_EQ(messageTypeFromName("pong"),         MSG_PONG);
    EXPECT_EQ(messageTypeFromName("welcome"),      MSG_WELCOME);
    EXPECT_EQ(messageTypeFromName("playerJoin"),   MSG_PLAYER_JOIN);
    EXPECT_EQ(messageTypeFromName("playerLeave"),  MSG_PLAYER_LEAVE);
    EXPECT_EQ(messageTypeFromName("state"),        MSG_STATE);
    EXPECT_EQ(messageTypeFromName("nonexistent"),  MSG_UNKNOWN);
    EXPECT_EQ(messageTypeFromName(""),             MSG_UNKNOWN);
}
