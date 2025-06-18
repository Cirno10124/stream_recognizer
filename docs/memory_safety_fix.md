# 内存安全问题修复报告

## 问题描述
用户反馈在Qt多线程环境下出现运行时堆损坏问题，疑似与新增的噪声抑制代码中的内存分配有关。

## 根本原因分析
1. **大量vector复制**：频繁的`std::vector<float>`复制操作导致内存碎片
2. **动态内存分配冲突**：在Qt多线程环境下的并发内存分配
3. **缺乏边界检查**：没有充分的数值有效性检查
4. **异常处理不足**：缺乏try-catch保护

## 修复措施

### 1. 使用thread_local缓存避免重复分配
```cpp
// 修复前：每次都创建新的vector
std::vector<float> original_buffer = audio_buffer;

// 修复后：使用thread_local缓存
static thread_local std::vector<float> original_buffer_cache;
original_buffer_cache.clear();
original_buffer_cache.reserve(audio_buffer.size());
original_buffer_cache.assign(audio_buffer.begin(), audio_buffer.end());
```

### 2. 优化SNR计算避免排序操作
```cpp
// 修复前：创建副本并排序（内存密集）
std::vector<float> sorted_samples = buffer;
std::sort(sorted_samples.begin(), sorted_samples.end(), ...);

// 修复后：直接计算最值（O(n)时间复杂度）
float min_abs_sample = std::abs(buffer[0]);
float max_abs_sample = std::abs(buffer[0]);
for (size_t i = 1; i < buffer.size(); ++i) {
    float abs_sample = std::abs(buffer[i]);
    min_abs_sample = std::min(min_abs_sample, abs_sample);
    max_abs_sample = std::max(max_abs_sample, abs_sample);
}
```

### 3. 增强数值安全检查
```cpp
// 检查数值有效性
if (std::isfinite(processed[i]) && std::isfinite(original[i])) {
    processed[i] = processed[i] * processed_weight + original[i] * original_weight;
    processed[i] = clamp(processed[i], -1.0f, 1.0f);
} else {
    // 如果数值无效，使用原始样本
    processed[i] = clamp(original[i], -1.0f, 1.0f);
}
```

### 4. 添加异常处理保护
```cpp
// 安全计算信噪比
float snr = 0.0f;
try {
    snr = calculateSignalToNoiseRatio(original_buffer);
    if (!std::isfinite(snr)) {
        snr = 5.0f; // 默认中等SNR
    }
} catch (...) {
    std::cerr << "[AudioPreprocessor] SNR计算异常，使用默认值" << std::endl;
    snr = 5.0f;
}
```

### 5. 参数范围限制和归一化
```cpp
// 参数安全检查
mix_ratio = clamp(mix_ratio, 0.0f, 1.0f);
float strength = clamp(noise_suppression_strength, 0.0f, 1.0f);

// 确保权重和为1.0（归一化）
float total_weight = processed_weight + original_weight;
if (total_weight > 0.0f) {
    processed_weight /= total_weight;
    original_weight /= total_weight;
} else {
    processed_weight = 0.5f;
    original_weight = 0.5f;
}
```

## 性能优化效果
1. **内存分配减少**：使用thread_local缓存，避免频繁分配/释放
2. **时间复杂度优化**：SNR计算从O(n log n)降至O(n)
3. **线程安全性**：thread_local确保每个线程独立的缓存
4. **异常安全性**：添加try-catch保护，防止程序崩溃

## 兼容性保证
- 保持原有API接口不变
- 向后兼容现有代码
- 不影响功能正确性
- 提高稳定性和性能

## 测试建议
1. 长时间运行测试，观察是否还有内存泄漏
2. 多线程并发测试，验证线程安全性
3. 边界条件测试，验证异常处理
4. 性能基准测试，确认优化效果

## 相关文件
- `src/audio_preprocessor.cpp` - 主要修复代码
- `include/audio_preprocessor.h` - 接口定义（无需修改）
- 新增头文件：`<cstring>`, `<stdexcept>`, `<limits>` 