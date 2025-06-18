# 噪声抑制统一控制机制

## 🎯 问题解决
已移除重复的噪声抑制控制逻辑，现在使用统一的控制点：

## 🔧 统一控制方式

### 单一控制点
```cpp
AudioPreprocessor preprocessor;
preprocessor.use_noise_suppression = false;  // 默认值，禁用RNNoise
```

### 控制逻辑
1. **默认状态**：`use_noise_suppression = false` （构造函数中设置）
2. **控制位置**：`applyNoiseSuppression()` 函数开头检查此变量
3. **调试信息**：`DEBUG_AUDIO_LEVELS = true` 提供详细的音频电平信息

## 📊 行为说明

### 禁用状态（默认）
```cpp
preprocessor.use_noise_suppression = false;
```
**结果**：
- 输出：`[AudioPreprocessor] 噪声抑制已禁用（use_noise_suppression=false），保持原始音频不变`
- 音频数据完全不变，直接返回
- VAD应该能正常检测语音活动

### 启用状态（调试用）
```cpp
preprocessor.use_noise_suppression = true;
preprocessor.initializeNoiseSuppressor();  // 初始化RNNoise
```
**结果**：
- 执行RNNoise处理
- 输出详细的处理前后音频电平信息
- 自动检测过度抑制并发出警告

## 🚀 优势

1. **统一控制**：只有一个控制点，避免冲突
2. **默认安全**：默认禁用，不会影响VAD
3. **易于调试**：清晰的日志输出
4. **兼容性好**：保持现有API不变

## 📋 使用示例

```cpp
// 创建预处理器
AudioPreprocessor preprocessor;

// 默认情况下噪声抑制已禁用，可以直接使用
std::vector<float> audio_data = {...};
preprocessor.process(audio_data, 16000);

// 如需启用噪声抑制（解决VAD问题后）
// preprocessor.use_noise_suppression = true;
// preprocessor.initializeNoiseSuppressor();
```

## 🔍 验证方法

编译运行后，应该看到：
```
[AudioPreprocessor] 处理前 - RMS: 0.025, 最大值: 0.15, 样本数: 160
[AudioPreprocessor] 噪声抑制已禁用（use_noise_suppression=false），保持原始音频不变
```

这确认了噪声抑制已被正确禁用，VAD问题应该得到解决。 