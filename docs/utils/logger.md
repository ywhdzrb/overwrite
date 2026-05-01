# 日志系统

日志系统提供分级日志记录功能。

---

## LogLevel

日志级别枚举。

```cpp
enum class LogLevel {
    DEBUG,
    INFO,
    WARNING,
    ERROR
};
```

---

## Logger

日志记录器类，提供静态方法。

### 类定义

```cpp
class Logger {
public:
    static void log(LogLevel level, const std::string& message);
    static void debug(const std::string& message);
    static void info(const std::string& message);
    static void warning(const std::string& message);
    static void error(const std::string& message);

private:
    static std::string getLevelString(LogLevel level);
    static std::string getCurrentTime();
};
```

### 使用示例

```cpp
using namespace owengine;

// 基本日志
Logger::debug("Debug message");      // [DEBUG] 2024-01-01 12:00:00 - Debug message
Logger::info("Info message");        // [INFO] 2024-01-01 12:00:00 - Info message
Logger::warning("Warning message");  // [WARNING] 2024-01-01 12:00:00 - Warning message
Logger::error("Error message");      // [ERROR] 2024-01-01 12:00:00 - Error message

// 使用不同日志级别
Logger::log(LogLevel::INFO, "Custom level message");
```

### 输出格式

```
[LEVEL] YYYY-MM-DD HH:MM:SS - Message
```

示例输出：
```
[INFO] 2024-01-15 14:30:22 - Application started
[DEBUG] 2024-01-15 14:30:22 - Loading model: player.glb
[WARNING] 2024-01-15 14:30:23 - Texture not found, using default
[ERROR] 2024-01-15 14:30:23 - Failed to create Vulkan instance
```

---

## 使用场景

### 调试信息

```cpp
Logger::debug("Vertex count: " + std::to_string(vertices.size()));
Logger::debug("FPS: " + std::to_string(fps));
```

### 状态信息

```cpp
Logger::info("Application initialized");
Logger::info("Connected to server: " + serverAddress);
Logger::info("Player joined: " + playerName);
```

### 警告

```cpp
Logger::warning("Low memory: " + std::to_string(availableMemory) + " MB");
Logger::warning("Missing texture, using fallback");
Logger::warning("Deprecated API call");
```

### 错误

```cpp
Logger::error("Failed to load file: " + filename);
Logger::error("Network connection lost");
Logger::error("Vulkan error: " + std::to_string(result));
```

---

## 最佳实践

### 1. 使用适当的级别

```cpp
// 好
Logger::debug("Processing vertex " + std::to_string(i));  // 详细调试信息
Logger::info("Model loaded successfully");                  // 重要状态
Logger::warning("Using default texture");                   // 可恢复的问题
Logger::error("Failed to initialize Vulkan");               // 严重错误

// 不好
Logger::error("Processing vertex");   // 应该用 DEBUG
Logger::debug("Critical error");      // 应该用 ERROR
```

### 2. 包含上下文信息

```cpp
// 好
Logger::error("Failed to load model: " + filename + " (reason: file not found)");

// 不好
Logger::error("Failed to load");
```

### 3. 避免频繁日志

```cpp
// 不好 - 每帧记录
void update(float deltaTime) {
    Logger::debug("Delta time: " + std::to_string(deltaTime));
}

// 好 - 仅在变化时记录
void onFpsChanged(int fps) {
    Logger::info("FPS: " + std::to_string(fps));
}
```

---

## 扩展建议

当前日志系统输出到标准输出。可以考虑以下扩展：

### 文件日志

```cpp
class Logger {
public:
    static void setLogFile(const std::string& filename);
    static void setOutputToConsole(bool enabled);
    static void setOutputToFile(bool enabled);
};
```

### 过滤级别

```cpp
class Logger {
public:
    static void setMinLevel(LogLevel level);  // 只记录 >= level 的日志
};
```

### 格式化

```cpp
// 使用流式接口
Logger::info() << "Player " << playerName << " scored " << score << " points";
```
