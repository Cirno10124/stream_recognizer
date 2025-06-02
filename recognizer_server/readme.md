# 语音识别服务器

这是一个简单的语音识别服务器，支持使用不同的识别模式处理音频文件。

## 功能特点

- 支持对音频文件进行语音识别
- 提供HTTP API接口
- 可配置的识别参数
- 支持文件上传和验证
- 灵活的配置选项

## 系统要求

- C++17兼容的编译器
- CMake 3.10或更高版本
- nlohmann/json库（包含在3rd_party目录中）

## 配置

服务器通过`config.json`文件进行配置，主要配置项包括：

```json
{
    "server": {
        "host": "0.0.0.0",          // 服务器主机地址
        "port": 8080,               // 服务器端口
        "cors": {
            "allow_origin": "*",    // CORS允许的来源
            "allow_methods": "POST, GET, OPTIONS",  // CORS允许的方法
            "allow_headers": "Content-Type"         // CORS允许的头部
        }
    },
    "recognition": {
        "model_path": "models/whisper-medium.bin",  // 识别模型路径
        "default_params": {
            "language": "auto",     // 默认语言设置
            "use_gpu": true,        // 是否使用GPU
            "beam_size": 5,         // beam search大小
            "temperature": 0.0      // 采样温度
        }
    },
    "storage": {
        "dir": "storage",           // 存储目录
        "min_file_size_bytes": 1048576  // 最小文件大小（1MB）
    },
    "logging": {
        "level": "info",            // 日志级别
        "file": "logs/server.log"   // 日志文件路径
    }
}
```

## 构建与运行

### 构建项目

```bash
mkdir build
cd build
cmake ..
make
```

### 运行服务器

```bash
./recognizer_server
```

或者指定配置文件：

```bash
./recognizer_server --config /path/to/config.json
```

## API接口

### 健康检查

```
GET /health
```

响应：
```
Service is healthy
```

### 语音识别

```
POST /api/recognize
```

请求体：
```json
{
    "audio_path": "/path/to/audio/file.wav",
    "language": "zh",              // 可选
    "use_gpu": true,               // 可选
    "beam_size": 5,                // 可选
    "temperature": 0.0             // 可选
}
```

响应：
```json
{
    "success": true,
    "text": "识别的文本",
    "confidence": 0.95
}
```

### 文件上传

```
POST /api/upload
```

请求体：二进制音频文件

响应：
```json
{
    "success": true,
    "message": "文件上传成功"
}
```

## 目录结构

```
recognizer_server/
├── 3rd_party/            # 第三方依赖
├── include/              # 头文件
│   ├── recognition_service.h
│   └── file_handler.h
├── src/                  # 源文件
│   ├── main.cpp
│   ├── recognition_service.cpp
│   └── file_handler.cpp
├── config.json           # 配置文件
├── CMakeLists.txt        # CMake配置
└── readme.md             # 说明文档
```

## 许可证

此项目采用MIT许可证。详见LICENSE文件。
