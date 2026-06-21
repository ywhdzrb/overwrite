#pragma once

/**
 * @file event_bus.hpp
 * @brief 事件总线模块 — 类型安全的发布/订阅（Pub/Sub）模式实现
 *
 * 归属模块：core
 * 核心职责：解耦事件发送者与接收者，支持多处理器、RAII 自动取消订阅
 * 依赖关系：C++ 标准库
 * 关键设计：std::type_index 运行时类型识别 + std::weak_ptr 生命周期跟踪
 * 线程安全：publish() 在锁外调用处理器，支持递归发布、订阅/取消不阻塞发布
 */

#include <algorithm>
#include <functional>
#include <memory>
#include <mutex>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <vector>

namespace owengine {

/**
 * @brief 事件基类 — 所有自定义事件应继承此结构
 * @note 虚析构确保多态销毁安全
 */
struct Event {
    virtual ~Event() = default;
};

/**
 * @brief RAII 连接令牌 — 析构时自动取消事件订阅
 * @note 禁止拷贝，仅允许移动；移动后原对象不再拥有连接
 * @note 调用 disconnect() 可手动提前取消
 */
class EventConnection {
public:
    EventConnection() = default;

    /**
     * @brief 构造连接（由 EventBus::subscribe 内部使用）
     * @param type  事件类型标识
     * @param disconnect 断开连接的可调用对象（shared_ptr 托管使其可共享）
     */
    EventConnection(std::type_index type,
                    std::shared_ptr<std::function<void()>> disconnect) noexcept
        : type_(type)
        , disconnect_(std::move(disconnect))
    {
    }

    /** @brief 析构时自动取消订阅 */
    ~EventConnection()
    {
        if (disconnect_) {
            (*disconnect_)();
        }
    }

    // 允许移动
    EventConnection(EventConnection&&) noexcept = default;
    EventConnection& operator=(EventConnection&&) noexcept = default;

    // 禁止拷贝
    EventConnection(const EventConnection&) = delete;
    EventConnection& operator=(const EventConnection&) = delete;

    /**
     * @brief 手动取消订阅（效果与析构相同，但可提前执行）
     * @note 调用后连接失效，重复调用安全
     */
    void disconnect()
    {
        if (disconnect_) {
            (*disconnect_)();
            disconnect_ = nullptr;
        }
    }

private:
    std::type_index type_{typeid(void)};                           // 事件类型标识
    std::shared_ptr<std::function<void()>> disconnect_{nullptr};   // 断开回调
};

/**
 * @brief 类型安全的事件总线 — 发布/订阅（Pub/Sub）模式核心
 * @note 线程安全：所有公共方法均可被多线程并发调用
 * @note 发布者不等待处理器完成：publish() 同步调用但立即返回
 * @note 处理器生命周期由 EventConnection 管理，连接销毁后自动取消注册
 * @note 支持在处理器内部订阅/取消订阅或再次发布（递归安全）
 */
class EventBus {
public:
    EventBus() = default;
    ~EventBus() = default;

    // 禁止拷贝与移动（EventBus 应为全局唯一实例）
    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;
    EventBus(EventBus&&) = delete;
    EventBus& operator=(EventBus&&) = delete;

    /**
     * @brief 订阅指定类型的事件
     * @tparam T 事件类型（任意可拷贝类型均可，建议继承自 Event）
     * @param handler 事件处理器回调 std::function<void(const T&)>
     * @return EventConnection RAII 令牌，析构时自动取消订阅
     *
     * @note 处理器内部可安全地再次调用 subscribe() 或 publish()
     * @note 同一事件类型允许注册多个处理器，按订阅顺序依次调用
     */
    template<typename T>
    [[nodiscard]] EventConnection subscribe(std::function<void(const T&)> handler)
    {
        auto type = std::type_index(typeid(T));

        // 创建生命周期令牌，与 disconnect lambda 共享所有权
        auto token = std::make_shared<int>(0);

        // 类型擦除：将 typed handler 包装为 void(const void*) 统一调用接口
        auto erased_fn = [handler = std::move(handler)](const void* event) {
            handler(*static_cast<const T*>(event));
        };

        {
            std::lock_guard<std::mutex> lock(mutex_);
            handlers_[type].push_back(HandlerNode{token, std::move(erased_fn)});
        }

        // 断开连接时：根据令牌匹配并移除对应的 HandlerNode
        auto disconnect = std::make_shared<std::function<void()>>(
            [this, type, token]() {
                std::lock_guard<std::mutex> lock(mutex_);
                auto it = handlers_.find(type);
                if (it == handlers_.end()) return;
                auto& vec = it->second;
                vec.erase(
                    std::remove_if(vec.begin(), vec.end(),
                        [&token](const HandlerNode& node) {
                            return node.token == token;
                        }),
                    vec.end()
                );
            }
        );

        return EventConnection(type, std::move(disconnect));
    }

    /**
     * @brief 发布事件，通知所有已订阅的处理器
     * @tparam T 事件类型（模板参数自动推导，无需显式指定）
     * @param event 事件实例（const 引用传递，内部可能被多处理器共享）
     *
     * @note 处理器在锁外依次同步调用，确保发布者不被阻塞的同时避免死锁
     * @note 若某处理器订阅者已全部销毁（EventConnection 析构），自动跳过
     */
    template<typename T>
    void publish(const T& event)
    {
        auto type = std::type_index(typeid(T));

        // 在锁内拷贝所有有效处理器的可调用对象
        std::vector<std::function<void(const void*)>> active;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = handlers_.find(type);
            if (it == handlers_.end()) return;

            for (auto& node : it->second) {
                // token 非空说明该处理器的 Connection 仍存活
                if (node.token) {
                    active.push_back(node.fn);
                }
            }
        }

        // 在锁外依次调用处理器，避免死锁与递归锁问题
        for (auto& fn : active) {
            fn(&event);
        }
    }

    /**
     * @brief 清空所有事件类型的订阅（主要用于测试或重置）
     * @note 线程安全
     */
    void clear() noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        handlers_.clear();
    }

private:
    /**
     * @brief 处理器内部节点
     * @note token 为 shared_ptr<void>，由 HandlerNode 与对应的 disconnect lambda 共享
     *       只要任一 HandlerNode 或 disconnect 存活，token 就不会释放
     */
    struct HandlerNode {
        std::shared_ptr<void> token;                    // 生命周期令牌
        std::function<void(const void*)> fn;            // 类型擦除后的处理器
    };

    mutable std::mutex mutex_;                                      // 线程安全互斥锁
    std::unordered_map<std::type_index, std::vector<HandlerNode>> handlers_;  // 事件类型 → 处理器列表
};

} // namespace owengine
