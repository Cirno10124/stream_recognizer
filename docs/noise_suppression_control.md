# 噪声抑制控制开关说明

## 概述
为了解决RNNoise导致VAD全静音的问题，使用`AudioPreprocessor`类的成员变量统一控制噪声抑制行为，避免重复的控制逻辑。

## 控制方式

### 统一控制点
```cpp
// AudioPreprocessor类成员变量（推荐使用）
bool use_noise_suppression = false;   // 🔧 主开关：完全启用/禁用RNNoise

// 调试开关（仅在applyNoiseSuppression函数内部）
const bool DEBUG_AUDIO_LEVELS = true; // 🔧 调试开关：输出音频电平信息
```

### RNNoise内部开关（仅当use_noise_suppression=true时生效）
```cpp
const bool use_16k_native = true;              // 16kHz原生处理
const bool use_high_quality_resampling = false; // 高质量重采样
const bool is_16k_specialized = false;         // 专用16kHz库
```

## 使用方法

### 方法1：直接设置成员变量（推荐）
```cpp
AudioPreprocessor preprocessor;
preprocessor.use_noise_suppression = false;  // 禁用噪声抑制
```

### 方法2：使用设置函数
```cpp
AudioPreprocessor preprocessor;
preprocessor.setUseNoiseSuppression(false);  // 禁用噪声抑制
```

## 使用场景

### 场景1：完全禁用噪声抑制（推荐当前使用）
```cpp
preprocessor.use_noise_suppression = false;
```
**效果**：音频直接通过，无任何处理，用于确认VAD问题是否由噪声抑制引起

### 场景2：测试RNNoise（调试用）
```cpp
preprocessor.use_noise_suppression = true;
```
**效果**：启用RNNoise处理，通过调试信息观察是否过度抑制

## 调试信息解读

### 正常音频信号
```
[AudioPreprocessor] 处理前 - RMS: 0.025, 最大值: 0.15, 样本数: 160
[AudioPreprocessor] 处理后 - RMS: 0.020, 最大值: 0.12
```

### 过度抑制警告
```
[AudioPreprocessor] 处理前 - RMS: 0.025, 最大值: 0.15, 样本数: 160
[AudioPreprocessor] RNNoise处理后 - RMS: 0.0001, 最大值: 0.005
[AudioPreprocessor] ⚠️  警告: RNNoise可能将音频过度抑制为静音！
```



## 问题排查步骤

1. **首先**：设置`use_noise_suppression = false`
   - 如果VAD恢复正常 → 确认是RNNoise过度抑制问题
   - 如果VAD仍然异常 → 问题在其他地方

2. **然后**：如需噪声抑制，逐步调试RNNoise的各种配置组合

## 性能影响

- **完全禁用**：无性能影响
- **RNNoise**：中等性能影响（~10-20% CPU）

## 建议配置

**当前推荐（解决VAD问题）**：
```cpp
AudioPreprocessor preprocessor;
preprocessor.use_noise_suppression = false;  // 禁用RNNoise
// DEBUG_AUDIO_LEVELS = true 在函数内部已设置，生产环境可改为false
```

**长期目标（获得专用16kHz库后）**：
```cpp
const bool ENABLE_RNNOISE = true;
const bool is_16k_specialized = true;
const bool DEBUG_AUDIO_LEVELS = false;
``` 