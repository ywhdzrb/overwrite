// 音频管理器实现
// 使用 miniaudio 引擎（单头文件库，CMake 构建时自动下载到 external/）
// 提供：音效加载、3D 空间音频、背景音乐循环、全局音量控制
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "core/audio_manager.hpp"
#include "utils/logger.hpp"

#include <unordered_map>
#include <vector>
#include <mutex>
#include <memory>
#include <glm/glm.hpp>

namespace owengine {

// ============================================================================
// ma_sound RAII 删除器：stop → uninit → delete
// ============================================================================
struct MaSoundDeleter {
    void operator()(ma_sound* s) const noexcept {
        if (s) {
            ma_sound_stop(s);
            ma_sound_uninit(s);
            delete s;
        }
    }
};
using UniqueMaSound = std::unique_ptr<ma_sound, MaSoundDeleter>;

// ============================================================================
// 每帧空闲声音清理：超过此阈值才扫描活跃列表，避免高频遍历
// ============================================================================
static constexpr size_t CLEANUP_THRESHOLD = 64;

// ============================================================================
// 已注册音效的信息结构
// ============================================================================
struct LoadedSoundInfo {
    std::string filepath;
    bool looping = false;
};

// ============================================================================
// PIMPL 实现结构体：封装所有 miniaudio 对象
// ============================================================================
struct AudioManagerImpl {
    // miniaudio 引擎（核心音频上下文，管理设备、混音、3D 听者）
    ma_engine engine;

    // 音效注册表：名称 → 文件路径 + 循环标记
    std::unordered_map<std::string, LoadedSoundInfo> sound_registry;

    // 活跃播放句柄 → RAII ma_sound
    std::unordered_map<uint64_t, UniqueMaSound> active_sounds;
    uint64_t next_handle = 1;

    // 线程安全锁（playSound / stop / updateFrame 可能跨线程调用）
    std::mutex mutex;

    // 听者状态缓存（updateFrame 时统一提交到引擎）
    glm::vec3 listener_pos{0.0f};
    glm::vec3 listener_forward{0.0f, 0.0f, -1.0f};
    glm::vec3 listener_up{0.0f, 1.0f, 0.0f};
    bool listener_dirty = true;

    // 最近一次检查后，经过的帧数累计（用于惰性清理）
    size_t frame_count = 0;

    // 引擎是否初始化成功
    bool engine_initialized = false;
};

// ============================================================================
// 构造 / 析构
// ============================================================================

AudioManager::AudioManager()
    : impl_(std::make_unique<AudioManagerImpl>())
    , initialized_(false) {
}

AudioManager::~AudioManager() {
    cleanup();
}

// ============================================================================
// init — 初始化 miniaudio 引擎
// ============================================================================

bool AudioManager::init() {
    if (initialized_) {
        Logger::warning("AudioManager::init() — 重复初始化，跳过");
        return true;
    }

    ma_engine_config config = ma_engine_config_init();
    // 设置 1 个 3D 听者（默认值，显式声明以强调 3D 支持）
    config.listenerCount = 1;

    ma_result result = ma_engine_init(&config, &impl_->engine);
    if (result != MA_SUCCESS) {
        Logger::error(std::string("AudioManager::init() — ma_engine 初始化失败: ") + ma_result_description(result));
        return false;
    }

    impl_->engine_initialized = true;
    initialized_ = true;
    Logger::info("AudioManager 初始化成功 (miniaudio)");
    return true;
}

// ============================================================================
// cleanup — 清理所有活跃音效和引擎资源
// ============================================================================

void AudioManager::cleanup() {
    if (!initialized_) return;
    initialized_ = false;

    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->active_sounds.clear();
        impl_->sound_registry.clear();
    }

    if (impl_->engine_initialized) {
        ma_engine_uninit(&impl_->engine);
        impl_->engine_initialized = false;
    }

    Logger::info("AudioManager 已清理");
}

// ============================================================================
// loadSound — 注册音效（验证文件存在，记录路径和循环标记）
// ============================================================================

bool AudioManager::loadSound(const std::string& name, const std::string& filepath, bool looping) {
    if (!initialized_) {
        Logger::error("AudioManager::loadSound() — 引擎未初始化，无法加载: " + name);
        return false;
    }

    // 验证文件可访问
    FILE* fp = fopen(filepath.c_str(), "rb");
    if (!fp) {
        Logger::error("AudioManager::loadSound() — 文件不存在: " + filepath);
        return false;
    }
    fclose(fp);

    // 注册到音效表
    impl_->sound_registry[name] = {filepath, looping};
    Logger::info(std::string("AudioManager 注册音效: ") + name + " -> " + filepath);
    return true;
}

// ============================================================================
// unloadSound — 从注册表移除音效
// ============================================================================

void AudioManager::unloadSound(const std::string& name) {
    impl_->sound_registry.erase(name);
}

// ============================================================================
// createSoundFromFile — 内部辅助：从文件创建 ma_sound 并配置
// ============================================================================

static UniqueMaSound createSoundFromFile(ma_engine* engine, const std::string& filepath,
                                         bool looping, bool spatialize, float volume,
                                         const glm::vec3* position3D = nullptr) {
    auto sound = UniqueMaSound(new ma_sound());

    ma_uint32 flags = looping ? MA_SOUND_FLAG_DECODE : MA_SOUND_FLAG_STREAM;

    ma_result result = ma_sound_init_from_file(engine, filepath.c_str(), flags, nullptr, nullptr, sound.get());
    if (result != MA_SUCCESS) {
        Logger::error(std::string("AudioManager — ma_sound 创建失败: ") + ma_result_description(result));
        return nullptr;
    }

    ma_sound_set_volume(sound.get(), volume);

    if (spatialize) {
        ma_sound_set_spatialization_enabled(sound.get(), MA_TRUE);
        if (position3D) {
            ma_sound_set_position(sound.get(), position3D->x, position3D->y, position3D->z);
        }
        ma_sound_set_min_distance(sound.get(), 1.0f);
        ma_sound_set_max_distance(sound.get(), 100.0f);
        ma_sound_set_rolloff(sound.get(), 1.0f);
    } else {
        ma_sound_set_spatialization_enabled(sound.get(), MA_FALSE);
    }

    if (looping) {
        ma_sound_set_looping(sound.get(), MA_TRUE);
    }

    ma_sound_start(sound.get());
    return sound;
}

// ============================================================================
// playSound — 播放 2D 音效
// ============================================================================

uint64_t AudioManager::playSound(const std::string& name, float volume) {
    if (!initialized_) return 0;

    auto it = impl_->sound_registry.find(name);
    if (it == impl_->sound_registry.end()) {
        Logger::warning("AudioManager::playSound() — 未注册的音效: " + name);
        return 0;
    }

    const auto& info = it->second;
    auto sound = createSoundFromFile(&impl_->engine, info.filepath, info.looping, false, volume);
    if (!sound) return 0;

    std::lock_guard<std::mutex> lock(impl_->mutex);
    uint64_t handle = impl_->next_handle++;
    impl_->active_sounds[handle] = std::move(sound);
    return handle;
}

// ============================================================================
// playSound3D — 播放 3D 空间音效
// ============================================================================

uint64_t AudioManager::playSound3D(const std::string& name, const glm::vec3& position, float volume) {
    if (!initialized_) return 0;

    auto it = impl_->sound_registry.find(name);
    if (it == impl_->sound_registry.end()) {
        Logger::warning("AudioManager::playSound3D() — 未注册的音效: " + name);
        return 0;
    }

    const auto& info = it->second;
    auto sound = createSoundFromFile(&impl_->engine, info.filepath, info.looping, true, volume, &position);
    if (!sound) return 0;

    std::lock_guard<std::mutex> lock(impl_->mutex);
    uint64_t handle = impl_->next_handle++;
    impl_->active_sounds[handle] = std::move(sound);
    return handle;
}

// ============================================================================
// playMusic — 播放背景音乐（流式加载 + 循环）
// ============================================================================

uint64_t AudioManager::playMusic(const std::string& filepath, float volume) {
    if (!initialized_) return 0;

    auto sound = UniqueMaSound(new ma_sound());
    ma_result result = ma_sound_init_from_file(
        &impl_->engine, filepath.c_str(),
        MA_SOUND_FLAG_STREAM, nullptr, nullptr, sound.get());

    if (result != MA_SUCCESS) {
        Logger::error(std::string("AudioManager::playMusic() — 创建失败: ") + ma_result_description(result));
        return 0;
    }

    ma_sound_set_volume(sound.get(), volume);
    ma_sound_set_looping(sound.get(), MA_TRUE);
    ma_sound_set_spatialization_enabled(sound.get(), MA_FALSE);
    ma_sound_start(sound.get());

    std::lock_guard<std::mutex> lock(impl_->mutex);
    uint64_t handle = impl_->next_handle++;
    impl_->active_sounds[handle] = std::move(sound);

    Logger::info("AudioManager 开始播放背景音乐: " + filepath);
    return handle;
}

// ============================================================================
// stop / stopAll
// ============================================================================

void AudioManager::stop(uint64_t handle) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->active_sounds.erase(handle);
}

void AudioManager::stopAll() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->active_sounds.clear();
}

// ============================================================================
// 音量控制
// ============================================================================

void AudioManager::setMasterVolume(float volume) {
    if (initialized_) {
        ma_engine_set_volume(&impl_->engine, volume);
    }
}

float AudioManager::getMasterVolume() const {
    if (initialized_) {
        return ma_engine_get_volume(&impl_->engine);
    }
    return 0.0f;
}

// ============================================================================
// 听者配置（缓存到 impl_，updateFrame 时统一提交）
// ============================================================================

void AudioManager::setListenerPosition(const glm::vec3& pos) {
    impl_->listener_pos = pos;
    impl_->listener_dirty = true;
}

void AudioManager::setListenerOrientation(const glm::vec3& forward, const glm::vec3& up) {
    impl_->listener_forward = forward;
    impl_->listener_up = up;
    impl_->listener_dirty = true;
}

// ============================================================================
// update — 更新指定 3D 音效的世界位置
// ============================================================================

void AudioManager::update(uint64_t handle, const glm::vec3& position) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto it = impl_->active_sounds.find(handle);
    if (it != impl_->active_sounds.end() && it->second) {
        ma_sound_set_position(it->second.get(), position.x, position.y, position.z);
    }
}

// ============================================================================
// updateFrame — 每帧调用
// 1. 同步听者状态到 miniaudio 引擎
// 2. 惰性清理已播放完毕的 ma_sound
// ============================================================================

void AudioManager::updateFrame() {
    if (!initialized_) return;

    // —— 同步听者 ——
    if (impl_->listener_dirty) {
        ma_engine_listener_set_position(&impl_->engine, 0,
            impl_->listener_pos.x, impl_->listener_pos.y, impl_->listener_pos.z);
        ma_engine_listener_set_direction(&impl_->engine, 0,
            impl_->listener_forward.x, impl_->listener_forward.y, impl_->listener_forward.z);
        ma_engine_listener_set_world_up(&impl_->engine, 0,
            impl_->listener_up.x, impl_->listener_up.y, impl_->listener_up.z);
        impl_->listener_dirty = false;
    }

    // —— 惰性清理已停止的音效 ——
    if (impl_->active_sounds.size() < CLEANUP_THRESHOLD) return;

    std::lock_guard<std::mutex> lock(impl_->mutex);
    for (auto it = impl_->active_sounds.begin(); it != impl_->active_sounds.end(); ) {
        if (it->second && !ma_sound_is_playing(it->second.get())) {
            it = impl_->active_sounds.erase(it);
        } else {
            ++it;
        }
    }
}

// ============================================================================
// isPlaying — 查询指定句柄是否仍在播放
// ============================================================================

bool AudioManager::isPlaying(uint64_t handle) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto it = impl_->active_sounds.find(handle);
    if (it == impl_->active_sounds.end() || !it->second) return false;
    return ma_sound_is_playing(it->second.get());
}

} // namespace owengine
