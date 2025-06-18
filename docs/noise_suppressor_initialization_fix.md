# 噪声抑制器初始化问题修复

## 问题描述
用户重启了噪声抑制功能（将 `use_noise_suppression` 设为 `true`），但发现 `noise_suppressor` 在调用时为空指针，导致噪声抑制功能无法正常工作。

## 根本原因分析
1. **构造函数初始化缺陷**：
   - `AudioPreprocessor` 构造函数中，`noise_suppressor` 被初始化为 `nullptr`
   - 即使 `use_noise_suppression = true`，也没有自动调用 `initializeNoiseSuppressor()`

2. **运行时检查不完整**：
   - `applyNoiseSuppression()` 函数检查了 `!noise_suppressor`，但没有尝试自动初始化
   - 只是简单返回，导致噪声抑制被跳过

## 修复方案

### 修复1：构造函数自动初始化
```cpp
// 在 AudioPreprocessor 构造函数末尾添加
if (use_noise_suppression) {
    std::cout << "[AudioPreprocessor] 构造函数中检测到噪声抑制已启用，自动初始化RNNoise..." << std::endl;
    if (initializeNoiseSuppressor()) {
        std::cout << "[AudioPreprocessor] ✅ 噪声抑制器初始化成功" << std::endl;
    } else {
        std::cout << "[AudioPreprocessor] ❌ 噪声抑制器初始化失败，将禁用噪声抑制功能" << std::endl;
        use_noise_suppression = false;
    }
}
```

### 修复2：运行时安全检查
```cpp
// 在 applyNoiseSuppression() 函数中添加
if (!noise_suppressor) {
    std::cout << "[AudioPreprocessor] 检测到噪声抑制器未初始化，尝试自动初始化..." << std::endl;
    if (!initializeNoiseSuppressor()) {
        std::cout << "[AudioPreprocessor] ❌ 自动初始化失败，跳过噪声抑制处理" << std::endl;
        return;
    }
    std::cout << "[AudioPreprocessor] ✅ 自动初始化成功" << std::endl;
}
```

## 修复效果
1. **构造时自动初始化**：当 `use_noise_suppression = true` 时，构造函数会自动初始化 RNNoise
2. **运行时容错处理**：即使构造时没有初始化，运行时也会自动尝试初始化
3. **错误处理改进**：初始化失败时会自动禁用噪声抑制功能，避免程序崩溃
4. **调试信息增强**：添加了详细的日志输出，便于问题排查

## 兼容性
- 保持了原有的 `setUseNoiseSuppression()` 接口不变
- 向后兼容现有代码
- 不影响其他预处理功能

## 测试建议
1. 启动程序后检查控制台输出是否显示"✅ 噪声抑制器初始化成功"
2. 测试音频处理是否正常应用噪声抑制
3. 验证在 RNNoise 不可用时是否能正常降级处理

## 相关文件
- `src/audio_preprocessor.cpp` - 主要修复代码
- `include/audio_preprocessor.h` - 接口定义（无需修改） 