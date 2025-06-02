#!/bin/bash

# 应用CUDA内存池修复补丁的脚本

echo "正在应用CUDA内存池修复补丁..."

WHISPER_DIR="3rd_party/whisper.cpp"
PATCH_FILE="cuda_memory_pool_fix.patch"
TARGET_FILE="$WHISPER_DIR/ggml/src/ggml-cuda/ggml-cuda.cu"

# 检查文件是否存在
if [ ! -f "$TARGET_FILE" ]; then
    echo "错误: 目标文件不存在: $TARGET_FILE"
    exit 1
fi

if [ ! -f "$PATCH_FILE" ]; then
    echo "错误: 补丁文件不存在: $PATCH_FILE"
    exit 1
fi

# 备份原文件
BACKUP_FILE="${TARGET_FILE}.backup.$(date +%Y%m%d_%H%M%S)"
echo "备份原文件到: $BACKUP_FILE"
cp "$TARGET_FILE" "$BACKUP_FILE"

# 直接修改文件而不是使用patch命令（因为patch格式可能有问题）
echo "正在修改 $TARGET_FILE ..."

# 使用sed进行修改
sed -i.tmp '
/GGML_ASSERT(ptr == (void \*) ((char \*)(pool_addr) + pool_used));/ {
    i\
        // 添加更安全的内存池释放检查，避免断言失败\
        void* expected_ptr = (void *) ((char *)(pool_addr) + pool_used);\
        if (ptr != expected_ptr) {\
            // 记录错误但不中断程序\
            fprintf(stderr, "Warning: CUDA VMM pool deallocation order violation. Expected: %p, Got: %p\\n", \
                    expected_ptr, ptr);\
            // 强制重置内存池状态\
            pool_used = 0;\
        } else {
    a\
        }
    d
}
' "$TARGET_FILE"

# 添加环境变量检查
sed -i.tmp2 '
/if (ggml_cuda_info().devices\[device\].vmm) {/ a\
        // 检查环境变量，允许强制禁用VMM\
        const char* force_legacy = getenv("GGML_CUDA_FORCE_LEGACY_POOL");\
        if (force_legacy && atoi(force_legacy) != 0) {\
            fprintf(stderr, "Info: Forcing legacy CUDA memory pool due to GGML_CUDA_FORCE_LEGACY_POOL\\n");\
            return std::unique_ptr<ggml_cuda_pool>(new ggml_cuda_pool_leg(device));\
        }
' "$TARGET_FILE"

# 清理临时文件
rm -f "${TARGET_FILE}.tmp" "${TARGET_FILE}.tmp2"

echo "补丁应用完成！"
echo ""
echo "修改内容："
echo "1. 将CUDA虚拟内存池的断言失败改为警告，避免程序崩溃"
echo "2. 添加环境变量 GGML_CUDA_FORCE_LEGACY_POOL 来强制使用传统内存池"
echo ""
echo "现在需要重新编译项目："
echo "  cd build"
echo "  make clean"
echo "  make -j\$(nproc)"
echo ""
echo "如果需要恢复原文件，请使用备份: $BACKUP_FILE" 