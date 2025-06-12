#!/usr/bin/env python3
"""
DeepSeek-Coder 7B vLLM部署配置
支持绝对路径和多种配置选项
适用于与Whisper-Large共存的3090服务器
"""

import os
import sys
import argparse
from pathlib import Path
from vllm import LLM, SamplingParams
from vllm.entrypoints.openai.api_server import run_server

def get_deployment_config(model_path=None):
    """获取部署配置"""
    # 默认模型路径（可以是绝对路径或HuggingFace模型名）
    default_model = model_path or "deepseek-ai/deepseek-coder-7b-instruct-v1.5"
    
    return {
        # 模型配置
        "model": default_model,
        "tokenizer": default_model,  # 通常与模型路径相同
        
        # 硬件配置
        "tensor_parallel_size": 1,  # 单GPU
        "gpu_memory_utilization": 0.75,  # 为Whisper预留显存
        "max_model_len": 4096,
        "block_size": 16,
        
        # 服务配置
        "host": "0.0.0.0",
        "port": 8000,
        "api_key": None,  # 可选：设置API密钥
        
        # 性能优化
        "quantization": None,  # None, "awq", "gptq"
        "dtype": "half",  # FP16精度
        "swap_space": 4,  # 4GB swap space
        
        # 推理配置
        "disable_log_stats": False,
        "max_num_seqs": 64,  # 最大并发序列
        "max_num_batched_tokens": 8192,
        "trust_remote_code": True,  # 信任远程代码
    }

def validate_model_path(model_path):
    """验证模型路径是否有效"""
    if model_path is None:
        return True  # 使用默认HuggingFace模型
    
    # 检查是否为绝对路径
    if os.path.isabs(model_path):
        if not os.path.exists(model_path):
            print(f"❌ 错误：模型路径不存在: {model_path}")
            return False
        
        # 检查是否包含必要的模型文件
        required_files = ["config.json"]
        for file in required_files:
            if not os.path.exists(os.path.join(model_path, file)):
                print(f"⚠️  警告：模型目录中缺少 {file}")
        
        print(f"✅ 模型路径验证通过: {model_path}")
        return True
    else:
        # HuggingFace模型名称
        print(f"📥 将从HuggingFace下载模型: {model_path}")
        return True

def check_dependencies():
    """检查必要的依赖"""
    required_packages = ["vllm", "torch", "transformers"]
    missing_packages = []
    
    for package in required_packages:
        try:
            __import__(package)
            print(f"✅ {package} 已安装")
        except ImportError:
            missing_packages.append(package)
            print(f"❌ {package} 未安装")
    
    if missing_packages:
        print(f"\n请安装缺失的包:")
        print(f"pip install {' '.join(missing_packages)}")
        return False
    
    return True

def deploy_with_vllm(config):
    """使用vLLM部署DeepSeek模型"""
    print("🚀 启动DeepSeek-Coder 7B部署...")
    print(f"📁 模型路径: {config['model']}")
    print(f"📊 GPU内存利用率: {config['gpu_memory_utilization']*100}%")
    print(f"🔧 量化方案: {config['quantization'] or '无'}")
    print(f"🌐 服务地址: http://{config['host']}:{config['port']}")
    print(f"🔢 最大序列长度: {config['max_model_len']}")
    
    # 设置环境变量
    os.environ["CUDA_VISIBLE_DEVICES"] = "0"  # 使用第一个GPU
    
    try:
        # 构建LLM参数
        llm_kwargs = {
            "model": config["model"],
            "tokenizer": config["tokenizer"],
            "tensor_parallel_size": config["tensor_parallel_size"],
            "gpu_memory_utilization": config["gpu_memory_utilization"],
            "max_model_len": config["max_model_len"],
            "dtype": config["dtype"],
            "swap_space": config["swap_space"],
            "max_num_seqs": config["max_num_seqs"],
            "max_num_batched_tokens": config["max_num_batched_tokens"],
            "trust_remote_code": config["trust_remote_code"],
        }
        
        # 只在有量化时添加量化参数
        if config["quantization"]:
            llm_kwargs["quantization"] = config["quantization"]
        
        print("🔄 正在加载模型...")
        
        # 初始化LLM
        llm = LLM(**llm_kwargs)
        
        print("✅ 模型加载成功！")
        print("🚀 启动API服务器...")
        
        # 启动API服务器
        run_server(
            engine=llm,
            host=config["host"],
            port=config["port"],
            api_key=config["api_key"],
            disable_log_stats=config["disable_log_stats"]
        )
        
    except Exception as e:
        print(f"❌ 部署失败: {e}")
        import traceback
        traceback.print_exc()
        raise

def main():
    parser = argparse.ArgumentParser(
        description="Deploy DeepSeek with vLLM",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例用法:
  # 使用HuggingFace模型
  python deepseek_vllm_config.py
  
  # 使用本地模型路径
  python deepseek_vllm_config.py --model /mnt/models/deepseek-coder-7b-instruct-v1.5
  
  # 自定义配置
  python deepseek_vllm_config.py --model /path/to/model --port 8001 --gpu-util 0.8 --quantization awq
        """
    )
    
    parser.add_argument("--model", type=str, 
                       help="模型路径（绝对路径）或HuggingFace模型名")
    parser.add_argument("--tokenizer", type=str, 
                       help="分词器路径（可选，默认与模型相同）")
    parser.add_argument("--port", type=int, default=8000, 
                       help="API服务端口 (默认: 8000)")
    parser.add_argument("--host", type=str, default="0.0.0.0", 
                       help="监听地址 (默认: 0.0.0.0)")
    parser.add_argument("--gpu-util", type=float, default=0.75, 
                       help="GPU内存利用率 (默认: 0.75)")
    parser.add_argument("--max-len", type=int, default=4096, 
                       help="最大序列长度 (默认: 4096)")
    parser.add_argument("--quantization", choices=["awq", "gptq", "none"], default="none", 
                       help="量化方案 (默认: none)")
    parser.add_argument("--dtype", choices=["half", "float16", "bfloat16", "float32"], default="half", 
                       help="数据类型 (默认: half)")
    parser.add_argument("--max-seqs", type=int, default=64, 
                       help="最大并发序列数 (默认: 64)")
    parser.add_argument("--check-deps", action="store_true", 
                       help="仅检查依赖而不启动服务")
    
    args = parser.parse_args()
    
    # 检查依赖
    if not check_dependencies():
        sys.exit(1)
    
    if args.check_deps:
        print("✅ 所有依赖检查完成！")
        return
    
    # 验证模型路径
    if not validate_model_path(args.model):
        sys.exit(1)
    
    # 更新配置
    config = get_deployment_config(args.model)
    config["port"] = args.port
    config["host"] = args.host
    config["gpu_memory_utilization"] = args.gpu_util
    config["max_model_len"] = args.max_len
    config["max_num_seqs"] = args.max_seqs
    config["dtype"] = args.dtype
    
    # 处理量化选项
    if args.quantization != "none":
        config["quantization"] = args.quantization
    else:
        config["quantization"] = None
    
    # 处理分词器路径
    if args.tokenizer:
        config["tokenizer"] = args.tokenizer
    
    try:
        deploy_with_vllm(config)
    except KeyboardInterrupt:
        print("\n🛑 用户中断，正在关闭服务...")
    except Exception as e:
        print(f"❌ 启动失败: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main() 