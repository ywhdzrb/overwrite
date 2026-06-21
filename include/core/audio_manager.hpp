#pragma once

#include <string>
#include <memory>
#include <glm/glm.hpp>

namespace owengine {

// 前置声明 PIMPL 实现结构体（隐藏 miniaudio 实现细节）
struct AudioManagerImpl;

/**
 * @brief 音频管理器
 *
 * 使用 miniaudio 引擎，支持 3D 空间音频。
 * 核心功能：
 * - 音效加载（WAV/MP3/OGG/FLAC）
 * - 3D 空间音效播放（位置追踪）
 * - 背景音乐循环
 * - 全局音量控制
 *
 * 生命周期：由 Application 持有，在初始化阶段调用 init()，
 * 每帧调用 updateFrame() 同步听者状态，析构时自动清理。
 *
 * 线程安全：playSound / stop / update 等操作内部加锁。
 */
class AudioManager {
public:
    AudioManager();
    ~AudioManager();

    // 禁止拷贝
    AudioManager(const AudioManager&) = delete;
    AudioManager& operator=(const AudioManager&) = delete;

    // ========== 生命周期 ==========

    /** @brief 初始化音频引擎，返回 true 成功 */
    bool init();

    /** @brief 清理所有资源 */
    void cleanup();

    // ========== 音效加载 ==========

    /**
     * @brief 从文件加载音效
     * @param name 音效唯一标识
     * @param filepath 音频文件路径
     * @param looping 是否循环播放
     * @return true 加载成功
     */
    bool loadSound(const std::string& name, const std::string& filepath, bool looping = false);

    /** @brief 卸载指定音效 */
    void unloadSound(const std::string& name);

    // ========== 播放控制 ==========

    /**
     * @brief 播放音效（无空间位置）
     * @param name 音效名称
     * @param volume 音量 [0.0, 1.0]
     * @return 播放句柄 ID（0 表示失败）
     */
    uint64_t playSound(const std::string& name, float volume = 1.0f);

    /**
     * @brief 播放 3D 空间音效
     * @param name 音效名称
     * @param position 世界空间位置
     * @param volume 音量 [0.0, 1.0]
     * @return 播放句柄 ID
     */
    uint64_t playSound3D(const std::string& name, const glm::vec3& position, float volume = 1.0f);

    /**
     * @brief 播放背景音乐（循环）
     * @param filepath 音频文件路径
     * @param volume 音量
     * @return 播放句柄 ID
     */
    uint64_t playMusic(const std::string& filepath, float volume = 0.5f);

    /** @brief 停止指定的播放句柄 */
    void stop(uint64_t handle);

    /** @brief 停止所有播放 */
    void stopAll();

    // ========== 参数控制 ==========

    void setMasterVolume(float volume);         // 全局音量 [0,1]
    float getMasterVolume() const;

    void setListenerPosition(const glm::vec3& pos);
    void setListenerOrientation(const glm::vec3& forward, const glm::vec3& up);

    /** @brief 更新 3D 音效位置（每帧调用） */
    void update(uint64_t handle, const glm::vec3& position);

    /** @brief 每帧更新（同步 listener 位置到引擎） */
    void updateFrame();

    // ========== 状态查询 ==========

    bool isPlaying(uint64_t handle) const;
    bool isInitialized() const { return initialized_; }

private:
    // PIMPL 隐藏 miniaudio 实现细节
    std::unique_ptr<AudioManagerImpl> impl_;
    bool initialized_ = false;
};

} // namespace owengine
