#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
快速测试GPU版本PaddleOCR
"""

import sys
import os

# 添加当前目录到路径
current_dir = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, current_dir)

def test_gpu_ocr():
    """测试GPU版本OCR"""
    try:
        from tools.ocr.improved_ocr_extractor import ImprovedOCRExtractor
        
        print("=== 快速GPU OCR测试 ===\n")
        
        # 初始化OCR提取器，启用GPU
        print("初始化GPU版本OCR提取器...")
        extractor = ImprovedOCRExtractor(
            languages=['ch', 'en'],
            use_gpu=True,  # 启用GPU
            engine='paddleocr'
        )
        
        if extractor.engine_name == 'paddleocr':
            print("✅ PaddleOCR GPU版本初始化成功!")
            return True
        else:
            print("❌ 未能初始化PaddleOCR")
            return False
            
    except Exception as e:
        print(f"❌ GPU OCR测试失败: {e}")
        import traceback
        traceback.print_exc()
        return False

def main():
    """主函数"""
    success = test_gpu_ocr()
    
    if success:
        print("\n🎉 GPU版本PaddleOCR测试成功!")
        print("\n现在可以使用GPU加速提取字幕:")
        print("python start_subtitle_wer_gui.py")
        print("或者使用命令行:")
        print("python tools/ocr/improved_ocr_extractor.py <视频文件> --confidence 0.2")
    else:
        print("\n❌ GPU版本测试失败")
        print("可能的解决方案:")
        print("1. 检查CUDA驱动是否正确安装")
        print("2. 重启Python环境")
        print("3. 使用CPU版本作为备选")

if __name__ == "__main__":
    main() 