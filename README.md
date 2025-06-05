# Stream Recognizer

实时音频流识别软件，支持离线和在线模式、实时字幕和多语言支持。

## 功能特性

- 实时音频流识别
- 支持OpenAI API在线识别
- 本地离线模式（使用whisper.cpp）
- 字幕生成与显示
- 多语言支持
- 实时分段处理

## 安装

### 前置条件

- Windows 10/11
- Visual Studio 2019 或更高版本
- Qt 6.5.0 或更高版本
- Python 3.8+ (用于OpenAI API服务)

### 编译步骤

1. 克隆仓库：
```
git clone https://github.com/username/stream_recognizer.git
cd stream_recognizer
```

2. 使用Visual Studio打开解决方案文件 `stream_recognizer.sln`

3. 编译项目（确保选择x64配置）

## 设置Python API服务器

如果要使用OpenAI API功能，需要设置本地Python API服务器。

### 安装Python依赖

```bash
pip install flask openai werkzeug uuid
```

### 设置OpenAI API密钥

确保设置了OpenAI API密钥：

```bash
# Windows CMD
set OPENAI_API_KEY=your_api_key_here

# Windows PowerShell
$env:OPENAI_API_KEY="your_api_key_here"
```

也可以在启动API服务器时通过参数提供：

```bash
python sre.py --api-key your_api_key_here
```

### 启动Python API服务器

```bash
python sre.py
```

默认情况下，服务器将在 `http://127.0.0.1:5000` 运行，并提供以下API端点：

- POST `/transcribe` - 音频文件转录
- POST `/stream_session` - 创建流式识别会话
- GET `/health` - 健康检查

## 使用软件

1. 启动Stream Recognizer应用程序。

2. 如果要使用OpenAI API进行识别：
   - 确保Python API服务器已启动
   - 在应用程序中勾选"使用OpenAI API"选项
   - 默认服务器URL为`http://127.0.0.1:5000/transcribe`，可以根据需要修改

3. 选择输入模式（麦克风、音频文件或视频文件）

4. 点击"开始识别"按钮

## 常见问题排查

### OpenAI API调用失败

如果收到404错误，请检查：

1. Python API服务器是否正在运行
2. 服务器URL是否正确（应包含"/transcribe"路径）
3. OpenAI API密钥是否正确设置

查看Python服务器的控制台输出以获取详细错误信息。


## 许可证

[在此添加许可证信息] 