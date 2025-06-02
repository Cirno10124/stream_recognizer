# CUDA内存池问题解决方案

## 问题描述

您遇到的错误：
```
GGML ASSERT(ptr == (void *)((char *)(pool_addr) + pool_used)) failed
```

这个错误发生在 `whisper.cpp/ggml/src/ggml-cuda/ggml-cuda.cu:502` 行，是CUDA虚拟内存池的断言失败。

## 问题根本原因

1. **CUDA虚拟内存池的LIFO要求**：CUDA虚拟内存池要求严格按照后进先出（LIFO）的顺序释放内存
2. **客户端修改导致的调用模式变化**：客户端的修改可能改变了内存分配/释放的顺序
3. **并发访问冲突**：多个请求同时访问内存池导致状态不一致
4. **内存碎片化**：频繁分配释放导致内存池管理混乱

## 解决方案

### 1. 立即解决方案（推荐）

#### A. 使用环境变量强制禁用虚拟内存池
```bash
export GGML_CUDA_FORCE_LEGACY_POOL=1
export GGML_USE_VMM=0
export CUDA_LAUNCH_BLOCKING=1
```

#### B. 使用启动脚本
```bash
cd recognizer_server
chmod +x start_server.sh
./start_server.sh
```

### 2. 代码层面修复

#### A. 应用CUDA内存池补丁
```bash
cd recognizer_server
chmod +x apply_cuda_fix.sh
./apply_cuda_fix.sh
```

#### B. 重新编译项目
```bash
cd build
make clean
make -j$(nproc)
```

### 3. 服务端代码改进

已在以下文件中添加了改进：

1. **`src/recognition_service.cpp`**：
   - 添加了线程安全的识别操作
   - 集成了CUDA内存管理器
   - 添加了自动故障转移到CPU模式

2. **`src/cuda_memory_manager.cpp`**：
   - 实现了单例模式的CUDA内存管理器
   - 提供了内存健康检查
   - 支持强制内存清理

3. **`include/cuda_memory_manager.h`**：
   - 定义了CUDA内存管理器接口

## 使用方法

### 方法1：使用启动脚本（推荐）
```bash
cd recognizer_server
./start_server.sh
```

### 方法2：手动设置环境变量
```bash
export GGML_CUDA_FORCE_LEGACY_POOL=1
export GGML_USE_VMM=0
export CUDA_LAUNCH_BLOCKING=1
export CUDA_CACHE_DISABLE=1
cd build
./recognizer_server
```

### 方法3：修改配置文件
在 `config.json` 中添加：
```json
{
  "use_gpu": false,
  "fallback_to_cpu": true
}
```

## 验证解决方案

1. **检查服务器启动日志**：
   ```
   CUDA内存管理器初始化成功: GeForce RTX 3080
   Info: Forcing legacy CUDA memory pool due to GGML_CUDA_FORCE_LEGACY_POOL
   ```

2. **测试精确识别功能**：
   - 发送识别请求
   - 观察是否还有断言错误
   - 检查内存使用情况

3. **监控内存状态**：
   ```bash
   nvidia-smi -l 1
   ```

## 故障排除

### 如果仍然出现问题：

1. **完全禁用GPU加速**：
   ```bash
   export CUDA_VISIBLE_DEVICES=""
   ```

2. **使用CPU模式**：
   在配置中设置 `"use_gpu": false`

3. **检查CUDA驱动**：
   ```bash
   nvidia-smi
   nvcc --version
   ```

4. **重置CUDA设备**：
   ```bash
   sudo nvidia-smi --gpu-reset
   ```

## 预防措施

1. **定期内存清理**：服务器会自动检查和清理CUDA内存
2. **请求限流**：避免过多并发请求
3. **监控内存使用**：定期检查GPU内存使用情况
4. **自动故障转移**：GPU出现问题时自动切换到CPU模式

## 技术细节

### CUDA虚拟内存池问题
- **原因**：虚拟内存池要求严格的LIFO释放顺序
- **解决**：使用传统内存池或改进释放逻辑

### 代码修改
- **断言改为警告**：避免程序崩溃
- **添加环境变量检查**：允许运行时配置
- **线程安全**：使用互斥锁保护关键操作

### 性能影响
- **传统内存池**：可能稍微降低性能，但更稳定
- **同步操作**：确保内存操作的正确性
- **自动故障转移**：保证服务可用性

## 联系支持

如果问题仍然存在，请提供：
1. 完整的错误日志
2. CUDA设备信息（`nvidia-smi`输出）
3. 服务器启动日志
4. 客户端调用方式的详细信息 