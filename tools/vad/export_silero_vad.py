#!/usr/bin/env python3
"""
Silero VAD模型导出脚本
将Silero VAD模型导出为ONNX格式，以便在C++中使用
"""

import torch
import onnx
import os
import sys
from pathlib import Path

def download_and_export_silero_vad():
    """下载并导出Silero VAD模型"""
    try:
        print("正在下载Silero VAD模型...")
        
        # 使用官方方法加载模型
        model, utils = torch.hub.load(
            repo_or_dir='snakers4/silero-vad',
            model='silero_vad',
            force_reload=True,
            onnx=False
        )
        
        print("模型下载完成，开始导出...")
        
        # 设置模型为评估模式
        model.eval()
        
        # 创建示例输入
        # Silero VAD期望的输入格式：(batch_size, sequence_length)
        batch_size = 1
        sequence_length = 512  # 32ms @ 16kHz
        dummy_input = torch.randn(batch_size, sequence_length)
        
        # 导出路径
        output_path = "silero_vad.onnx"
        
        print(f"导出ONNX模型到: {output_path}")
        
        # 导出到ONNX
        torch.onnx.export(
            model,
            dummy_input,
            output_path,
            export_params=True,
            opset_version=11,
            do_constant_folding=True,
            input_names=['input'],
            output_names=['output'],
            dynamic_axes={
                'input': {0: 'batch_size'},
                'output': {0: 'batch_size'}
            }
        )
        
        # 验证导出的模型
        print("验证ONNX模型...")
        onnx_model = onnx.load(output_path)
        onnx.checker.check_model(onnx_model)
        
        print(f"✅ ONNX模型导出成功: {output_path}")
        print(f"📁 模型大小: {os.path.getsize(output_path) / 1024 / 1024:.2f} MB")
        
        # 创建models目录并移动模型
        models_dir = Path("models")
        models_dir.mkdir(exist_ok=True)
        
        final_path = models_dir / "silero_vad.onnx"
        if os.path.exists(output_path):
            import shutil
            shutil.move(output_path, final_path)
            print(f"📂 模型已移动到: {final_path}")
        
        return True
        
    except Exception as e:
        print(f"❌ 导出失败: {e}")
        return False

def test_exported_model():
    """测试导出的ONNX模型"""
    try:
        import onnxruntime as ort
        
        model_path = "models/silero_vad.onnx"
        if not os.path.exists(model_path):
            print(f"❌ 模型文件不存在: {model_path}")
            return False
        
        print("测试ONNX模型...")
        
        # 创建推理会话
        session = ort.InferenceSession(model_path)
        
        # 创建测试输入
        test_input = torch.randn(1, 512).numpy()
        
        # 运行推理
        outputs = session.run(None, {'input': test_input})
        
        print(f"✅ 模型测试成功!")
        print(f"📊 输出形状: {outputs[0].shape}")
        print(f"📊 输出值范围: [{outputs[0].min():.4f}, {outputs[0].max():.4f}]")
        
        return True
        
    except Exception as e:
        print(f"❌ 模型测试失败: {e}")
        return False

if __name__ == "__main__":
    print("=" * 50)
    print("Silero VAD ONNX导出工具")
    print("=" * 50)
    
    # 检查依赖
    try:
        import torch
        import onnx
        print(f"✅ PyTorch版本: {torch.__version__}")
        print(f"✅ ONNX版本: {onnx.__version__}")
    except ImportError as e:
        print(f"❌ 缺少依赖: {e}")
        print("请运行: pip install torch onnx")
        sys.exit(1)
    
    # 导出模型
    if download_and_export_silero_vad():
        # 测试模型
        try:
            import onnxruntime
            test_exported_model()
        except ImportError:
            print("⚠️  onnxruntime未安装，跳过模型测试")
            print("如需测试，请运行: pip install onnxruntime")
    
    print("=" * 50)
    print("导出完成!")
    print("=" * 50) 