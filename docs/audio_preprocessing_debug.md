# 音频预处理链调试指南

## 🚨 问题诊断
VAD全静音问题可能由音频预处理链中的某个环节引起。已临时禁用所有预处理步骤进行排查。

## 🔧 当前配置（全部禁用）
```cpp
use_pre_emphasis = false;     // 预加重
use_high_pass = false;        // 高通滤波  
use_agc = false;              // AGC自动增益控制 ⚠️ 重点怀疑对象
use_compression = false;      // 动态范围压缩
use_noise_suppression = false; // 噪声抑制
use_final_gain = false;       // 最终增益放大
```

## 🔍 调试输出说明

### 正常情况应该看到：
```
[AudioPreprocessor::process] 原始音频 - RMS: 0.025, 最大值: 0.15, 样本数: 160
[AudioPreprocessor::process] 最终音频 - RMS: 0.025, 最大值: 0.15, 变化: 100.0%
=====================================
```

### 异常情况（音频被破坏）：
```
[AudioPreprocessor::process] 原始音频 - RMS: 0.025, 最大值: 0.15, 样本数: 160
[AudioPreprocessor::process] 最终音频 - RMS: 0.001, 最大值: 0.01, 变化: 4.0%
=====================================
```

## 🧐 重点怀疑的问题

### 1. AGC算法缺陷
```cpp
// 问题代码：
float alpha_attack = (rms > current_gain * rms) ? attack_time : release_time;
```
**问题**：条件 `rms > current_gain * rms` 逻辑错误
**后果**：可能导致增益计算异常

### 2. AGC参数不合理
```cpp
target_level(0.1f)    // 目标电平过低
min_gain(0.1f)        // 最小增益0.1会严重衰减信号
max_gain(10.0f)       // 最大增益过高
```

### 3. 压缩算法问题
```cpp
float gain_reduction = 1.0f + (compression_threshold - abs_sample) * 
                      (1.0f - 1.0f/compression_ratio) / compression_threshold;
```
**可能问题**：增益减少计算可能有误

## 📋 排查步骤

### 第1步：验证禁用状态
编译运行，确认所有预处理都被跳过，音频保持原样。

### 第2步：逐个启用测试
如果第1步正常，逐个启用预处理步骤：

1. **启用预加重**：`use_pre_emphasis = true`
2. **启用高通滤波**：`use_high_pass = true`  
3. **启用AGC**：`use_agc = true` ⚠️ 重点观察
4. **启用压缩**：`use_compression = true`

### 第3步：修复问题算法
找到问题环节后，修复相应算法。

## 🛠️ 预期修复方案

### AGC算法修复
```cpp
// 修复后的AGC逻辑
float desired_gain = target_level / (rms + 1e-6f);
desired_gain = clamp(desired_gain, 0.5f, 3.0f);  // 更合理的增益范围

// 修复平滑逻辑
float alpha = (desired_gain > current_gain) ? attack_time : release_time;
current_gain = alpha * desired_gain + (1.0f - alpha) * current_gain;
```

### 参数调整
```cpp
target_level(0.3f)    // 提高目标电平
min_gain(0.5f)        // 提高最小增益
max_gain(3.0f)        // 降低最大增益
```

## 🎯 成功标准
修复后应该看到：
- 原始音频电平保持合理范围
- VAD能正常检测语音活动
- 音频质量不受明显影响 