// cuda_override.h
// 这个文件用于覆盖或重新定义CUDA相关函数，以解决链接错误

#pragma once

// 确保这个文件被包含在ggml-cuda.cu之前
// 用于禁用或替换有问题的函数

// 禁用flash attention相关功能
#ifndef GGML_CUDA_NO_FLASHATTN
#define GGML_CUDA_NO_FLASHATTN 1
#endif

#ifndef GGML_FLASH_ATTN_EXT
#define GGML_FLASH_ATTN_EXT 0
#endif

#ifndef GGML_FLASH_ATTN_EXT_MMA_F16
#define GGML_FLASH_ATTN_EXT_MMA_F16 0 
#endif

#ifndef GGML_FLASH_ATTN_EXT_MMA_F32
#define GGML_FLASH_ATTN_EXT_MMA_F32 0
#endif

// 禁用矩阵乘法相关功能
#ifndef GGML_CUDA_NO_MMQ
#define GGML_CUDA_NO_MMQ 1
#endif

#ifndef GGML_CUDA_MMQ_DISABLE
#define GGML_CUDA_MMQ_DISABLE 1
#endif

#ifndef GGML_CUDA_NO_MAT_MUL
#define GGML_CUDA_NO_MAT_MUL 1
#endif

#ifndef GGML_CUDA_NO_MUL_MAT_VEC
#define GGML_CUDA_NO_MUL_MAT_VEC 1
#endif

// 禁用或替换DMMV相关功能
#ifndef GGML_CUDA_DMMV_X
#define GGML_CUDA_DMMV_X 1
#endif

#ifndef GGML_CUDA_FORCE_SIMPLE
#define GGML_CUDA_FORCE_SIMPLE 1
#endif

#ifndef GGML_CUDA_DMMV_USE_SIMPLE
#define GGML_CUDA_DMMV_USE_SIMPLE 1
#endif

// 强制使用简单的CUDA实现
#ifndef GGML_CUDA_DMMV_TYPE
#define GGML_CUDA_DMMV_TYPE GGML_CUDA_DMMV_SIMPLE
#endif

// 其他可能需要的宏定义
#define WHISPER_NO_FLASH_ATTN 1

// 这些函数将被替换为空实现
#define ggml_cuda_mul_mat_vec(...) 
#define ggml_cuda_flash_attn_ext(...) 