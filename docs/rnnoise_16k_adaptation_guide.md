# RNNoise 16kHz 适配方案指南

## 概述

针对固定16kHz采样率环境，我们提供了多种RNNoise适配方案，从最佳质量到快速实现都有覆盖。

## 方案对比

| 方案 | 质量 | 性能 | 实现复杂度 | 推荐场景 |
|------|------|------|-----------|----------|
| 专用16kHz库 | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ | ⭐⭐⭐ | 生产环境 |
| 高质量重采样 | ⭐⭐⭐⭐ | ⭐⭐⭐ | ⭐⭐ | 质量优先 |
| 原版库适配 | ⭐⭐⭐ | ⭐⭐⭐⭐ | ⭐ | 当前实现 |
| 简单噪声门 | ⭐⭐ | ⭐⭐⭐⭐⭐ | ⭐ | 备用方案 |

## 方案详述

### 方案1：专用16kHz RNNoise库 🏆 **强烈推荐**

**优势：**
- 专门为16kHz训练，效果最佳
- 原生支持160样本帧（10ms@16kHz）
- 无需重采样，性能最优
- 完全适配16kHz音频特性

**实现步骤：**

1. **下载16kHz专用库**
```bash
# 方案A: YongyuG/rnnoise_16k (推荐)
git clone https://github.com/YongyuG/rnnoise_16k.git

# 方案B: mangwaier/16KRNNoise
git clone https://github.com/mangwaier/16KRNNoise.git
```

2. **Windows编译**
```bash
cd rnnoise_16k
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

3. **集成到项目**
- 替换现有的`rnnoise.h`和`rnnoise.lib`
- 在代码中设置`is_16k_specialized = true`
- 帧大小自动调整为160样本

### 方案2：高质量重采样

**优势：**
- 使用原版RNNoise的完整功能
- Lanczos重采样保证高质量
- 可配置重采样参数

**配置：**
```cpp
const bool use_high_quality_resampling = true;
```

**性能影响：**
- CPU使用增加约30-50%
- 内存使用增加约3倍（临时缓冲区）
- 延迟增加约5-10ms

### 方案3：原版库适配（当前实现）

**优势：**
- 使用现有库，无需额外下载
- 实现简单，兼容性好
- 性能开销较小

**配置：**
```cpp
const bool use_16k_native = true;
const bool is_16k_specialized = false;
```

### 方案4：简单噪声门（备用）

**优势：**
- 无需RNNoise库
- 性能开销最小
- 实现最简单

**配置：**
```cpp
// 在audio_preprocessor.cpp中禁用RNNOISE_AVAILABLE
// 或设置use_noise_suppression = false
```

## 配置方法

### 代码配置

在`src/audio_preprocessor.cpp`中修改以下参数：

```cpp
void AudioPreprocessor::applyNoiseSuppression(std::vector<float>& audio_buffer) {
    // 方案选择配置
    const bool use_16k_native = true;              // 使用16kHz处理
    const bool use_high_quality_resampling = false; // 使用高质量重采样
    const bool is_16k_specialized = false;         // 是否为16kHz专用库
    
    // 根据需求修改上述参数
}
```

### 运行时配置

```cpp
// 启用噪声抑制
audio_preprocessor->setUseNoiseSuppression(true);

// 或直接设置
audio_preprocessor->use_noise_suppression = true;
```

## 性能调优

### CPU使用优化
```cpp
// 降低处理频率（如果可接受）
if (frame_count % 2 == 0) {  // 每隔一帧处理
    applyNoiseSuppression(audio_buffer);
}
```

### 内存使用优化
```cpp
// 使用固定大小缓冲区避免频繁分配
static std::vector<float> reuse_buffer;
reuse_buffer.resize(audio_buffer.size());
```

## 质量评估

### 测试方法
1. 录制包含语音和噪声的测试音频
2. 分别使用不同方案处理
3. 对比处理前后的：
   - 语音清晰度
   - 噪声抑制效果
   - VAD检测准确性

### 评估指标
- **SNR改善**：信噪比提升程度
- **语音保真度**：语音质量损失程度
- **VAD准确性**：语音活动检测准确率
- **处理延迟**：实时性要求

## 故障排除

### 常见问题

1. **VAD全部标记为静音**
   - 检查RMS计算是否正确
   - 确认噪声抑制强度不过高
   - 验证音频数据格式

2. **音质下降明显**
   - 尝试降低噪声抑制强度
   - 检查重采样质量
   - 考虑混合原始信号

3. **性能问题**
   - 选择更轻量的方案
   - 优化处理频率
   - 使用多线程处理

### 调试输出

启用详细日志：
```cpp
std::cout << "[AudioPreprocessor] 原始RMS: " << original_rms << std::endl;
std::cout << "[AudioPreprocessor] 处理后RMS: " << processed_rms << std::endl;
std::cout << "[AudioPreprocessor] 抑制比例: " << (1.0f - processed_rms/original_rms) << std::endl;
```

## 建议的实施路径

### 阶段1：验证当前方案
1. 使用原版库适配方案测试
2. 验证VAD问题是否解决
3. 评估基本噪声抑制效果

### 阶段2：升级到专用库
1. 下载并编译16kHz专用RNNoise
2. 替换库文件并测试
3. 对比效果改善程度

### 阶段3：优化和调优
1. 根据实际使用场景调整参数
2. 实施性能优化措施
3. 建立质量监控机制

## 总结

对于16kHz固定采样率环境，**强烈推荐使用专用16kHz RNNoise库**，这将提供最佳的性能和质量平衡。当前的适配方案可以作为过渡解决方案使用。 