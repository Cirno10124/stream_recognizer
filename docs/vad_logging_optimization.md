# VAD日志优化说明

## 优化目标
减少VAD产生的大量日志输出，只保留关键事件的记录，提高日志的可读性。

## 主要改进

### 1. 状态变化日志优化
**之前**: 每帧都可能输出检测结果
**现在**: 只在语音/静音状态发生变化时输出

```
[VAD] 状态变化 #1: 静音 → 语音 (能量:0.0045, 阈值:0.002, WebRTC:语音, 帧间隔:23)
[VAD] 状态变化 #2: 语音 → 静音 (能量:0.0008, 阈值:0.002, WebRTC:静音, 帧间隔:156)
```

### 2. 智能分段日志优化
**之前**: 频繁输出分段检测信息
**现在**: 只在真正触发智能分段时输出

```
[VAD] 🎯 智能分段触发: 连续40帧静音, 历史语音帧:25 (最近:3)
```

### 3. 初始化日志简化
**之前**: 详细的初始化步骤日志
**现在**: 简化为关键信息

```
[VAD] 初始化WebRTC VAD (模式:1, 帧数:5/15)...
[VAD] WebRTC VAD初始化完成 ✓
```

### 4. 参数设置日志优化
**之前**: 每次设置都输出
**现在**: 只在参数真正改变时输出

```
[VAD] 最小语音帧数: 5 → 8
[VAD] 能量阈值: 0.0020 → 0.0050
[VAD] 自适应模式: 禁用 → 启用
```

### 5. 调试信息频率控制
**之前**: 每帧都可能输出调试信息
**现在**: 每100帧输出一次，避免日志泛滥

```
[VAD] 语音帧不足(7/10)，等待更多语音  // 每100帧输出一次
```

## 日志级别说明

### 🔹 信息级别 (std::cout)
- VAD状态变化
- 智能分段触发
- 参数变化
- 初始化完成

### 🔸 警告级别 (std::cerr)
- 配置失败
- 实例创建失败
- 库状态异常

### 🔇 静默处理
- 正常的帧级检测
- 成功的内部操作
- 重复的状态确认

## 使用效果

### 优化前日志示例:
```
[VAD] 检测帧能量: 0.0012
[VAD] WebRTC结果: 0
[VAD] 当前状态: 静音
[VAD] 静音计数: 15
[VAD] 检测帧能量: 0.0011
[VAD] WebRTC结果: 0
[VAD] 当前状态: 静音
[VAD] 静音计数: 16
... (大量重复日志)
```

### 优化后日志示例:
```
[VAD] 初始化WebRTC VAD (模式:1, 帧数:5/15)...
[VAD] WebRTC VAD初始化完成 ✓
[VAD] 状态变化 #1: 静音 → 语音 (能量:0.0045, 阈值:0.002, WebRTC:语音, 帧间隔:23)
[VAD] 🎯 智能分段触发: 连续40帧静音, 历史语音帧:25 (最近:3)
[VAD] 状态变化 #2: 语音 → 静音 (能量:0.0008, 阈值:0.002, WebRTC:静音, 帧间隔:156)
```

## 配置建议

在需要详细调试时，可以临时启用更详细的日志：
1. 修改调试计数器频率（从100改为1）
2. 在关键检测点添加临时日志
3. 使用条件编译控制调试级别

这样既保持了日志的简洁性，又保留了必要的调试信息获取能力。 