#!/bin/bash

# 语音识别服务器启动脚本
# 包含CUDA内存池问题的解决方案

echo "正在启动语音识别服务器..."

# 设置CUDA环境变量以解决内存池问题
export CUDA_LAUNCH_BLOCKING=1          # 同步CUDA操作，避免并发问题
export CUDA_CACHE_DISABLE=1            # 禁用CUDA缓存，减少内存碎片
export GGML_USE_VMM=0                   # 禁用虚拟内存池，使用传统内存池
export GGML_CUDA_FORCE_LEGACY_POOL=1   # 强制使用传统内存池

# 设置CUDA内存管理参数
export CUDA_DEVICE_MAX_CONNECTIONS=1   # 限制设备连接数
export CUDA_VISIBLE_DEVICES=0          # 只使用第一个GPU设备

# 设置日志级别
export GGML_LOG_LEVEL=2                # 设置详细日志级别

echo "CUDA环境变量已设置："
echo "  CUDA_LAUNCH_BLOCKING=$CUDA_LAUNCH_BLOCKING"
echo "  CUDA_CACHE_DISABLE=$CUDA_CACHE_DISABLE"
echo "  GGML_USE_VMM=$GGML_USE_VMM"
echo "  GGML_CUDA_FORCE_LEGACY_POOL=$GGML_CUDA_FORCE_LEGACY_POOL"
echo "  CUDA_DEVICE_MAX_CONNECTIONS=$CUDA_DEVICE_MAX_CONNECTIONS"
echo "  CUDA_VISIBLE_DEVICES=$CUDA_VISIBLE_DEVICES"

# 检查CUDA设备状态
echo ""
echo "检查CUDA设备状态..."
if command -v nvidia-smi &> /dev/null; then
    nvidia-smi --query-gpu=name,memory.total,memory.used,memory.free --format=csv,noheader,nounits
else
    echo "警告: nvidia-smi 未找到，无法检查GPU状态"
fi

# 检查必要的文件
echo ""
echo "检查必要文件..."

CONFIG_FILE="../config.json"
if [ ! -f "$CONFIG_FILE" ]; then
    echo "警告: 配置文件不存在: $CONFIG_FILE"
    echo "将使用默认配置"
fi

BUILD_DIR="./build"
if [ ! -d "$BUILD_DIR" ]; then
    echo "错误: 构建目录不存在: $BUILD_DIR"
    echo "请先运行 cmake 构建项目"
    exit 1
fi

EXECUTABLE="$BUILD_DIR/recognizer_server"
if [ ! -f "$EXECUTABLE" ]; then
    echo "错误: 可执行文件不存在: $EXECUTABLE"
    echo "请先编译项目"
    exit 1
fi

# 创建必要的目录
echo ""
echo "创建必要目录..."
mkdir -p logs
mkdir -p storage
mkdir -p temp

# 清理旧的临时文件
echo "清理临时文件..."
find storage -name "tmp_*" -type f -delete 2>/dev/null || true
find temp -name "*" -type f -delete 2>/dev/null || true

# 设置核心转储（用于调试）
ulimit -c unlimited

# 设置内存限制（防止内存泄漏）
ulimit -v 8388608  # 8GB虚拟内存限制

echo ""
echo "启动服务器..."
echo "如果遇到CUDA内存池错误，服务器会自动切换到CPU模式"
echo "按 Ctrl+C 停止服务器"
echo ""

# 启动服务器，并将输出重定向到日志文件
exec "$EXECUTABLE" --config "$CONFIG_FILE" 2>&1 | tee logs/server_$(date +%Y%m%d_%H%M%S).log 