# RNNoise过度抑制问题修复方案

## 问题描述
用户反馈RNNoise过度抑制了原音频，导致音频能量削弱了3-5个数量级，使得VAD（语音活动检测）误判为静音。这会严重影响语音识别的效果。

## 根本原因分析
1. **RNNoise默认行为**：RNNoise设计用于强力去除噪声，在处理语音时可能过于激进
2. **16kHz适配问题**：原版RNNoise针对48kHz优化，处理16kHz音频时可能出现过度抑制
3. **缺乏强度控制**：原实现没有提供噪声抑制强度的精细控制
4. **没有VAD兼容性考虑**：处理后的音频能量过低，影响后续VAD判断

## 修复方案

### 1. 添加噪声抑制强度控制
```cpp
// 新增参数
float noise_suppression_strength;     // 噪声抑制强度（0.0-1.0）
float noise_suppression_mix_ratio;    // 原始音频与处理音频的混合比例
bool use_adaptive_suppression;        // 自适应抑制开关
float vad_energy_threshold;           // VAD能量阈值
```

### 2. 自适应噪声抑制算法
- **信噪比检测**：自动计算音频的信噪比
- **动态调整**：根据信噪比调整噪声抑制强度
- **VAD保护**：检测处理后信号是否低于VAD阈值，自动调整

### 3. 音频混合策略
- **智能混合**：将处理后的音频与原始音频按比例混合
- **能量保护**：确保处理后音频保持足够的能量用于VAD检测
- **削波保护**：防止混合后音频出现削波失真

## 默认参数配置
```cpp
noise_suppression_strength = 0.6f;    // 中等强度（60%）
noise_suppression_mix_ratio = 0.2f;   // 80%处理音频 + 20%原始音频
use_adaptive_suppression = true;       // 启用自适应模式
vad_energy_threshold = 0.001f;         // VAD能量阈值
```

## 工作流程
1. **保存原始音频**：在处理前备份原始音频
2. **RNNoise处理**：使用现有的RNNoise算法处理音频
3. **信噪比分析**：计算原始音频的信噪比
4. **自适应调整**：
   - 高SNR（>10dB）：减少抑制强度，保持更多原始信号
   - 低SNR（<3dB）：增加抑制强度，更积极地去噪
5. **VAD保护检查**：如果处理后信号过弱，自动增加原始信号比例
6. **音频混合**：按计算出的比例混合处理音频和原始音频

## 使用方法

### 基本设置
```cpp
// 设置噪声抑制参数
audio_preprocessor->setNoiseSuppressionParameters(
    0.6f,    // 强度：60%
    0.2f,    // 混合比例：20%原始音频
    true     // 启用自适应模式
);

// 设置VAD阈值
audio_preprocessor->setVADEnergyThreshold(0.001f);
```

### 场景优化
```cpp
// 清洁环境（高SNR）
audio_preprocessor->setNoiseSuppressionParameters(0.4f, 0.3f, true);

// 嘈杂环境（低SNR）
audio_preprocessor->setNoiseSuppressionParameters(0.8f, 0.1f, true);

// 保守模式（保护语音质量）
audio_preprocessor->setNoiseSuppressionParameters(0.5f, 0.4f, true);
```

## 预期效果
1. **解决过度抑制**：通过混合原始音频，保持足够的信号能量
2. **VAD兼容性**：确保处理后音频能被VAD正确识别
3. **自适应性能**：根据音频质量自动调整处理强度
4. **可调节性**：提供丰富的参数供用户精细调节

## 调试信息
修复后的系统会输出详细的调试信息：
- 处理前后的RMS和最大值
- 信噪比计算结果
- 自适应调整的混合比例
- VAD阈值检查结果

## 相关文件
- `include/audio_preprocessor.h` - 新增参数和方法声明
- `src/audio_preprocessor.cpp` - 主要实现代码
- `docs/noise_suppressor_initialization_fix.md` - 初始化问题修复 