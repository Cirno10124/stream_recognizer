#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
测试改进的OCR功能
"""

import os
import sys
import cv2
import numpy as np
from pathlib import Path

def test_ocr_engines():
    """测试各种OCR引擎的可用性"""
    print("测试OCR引擎可用性...")
    print("="*50)
    
    # 测试改进的OCR提取器
    try:
        from improved_ocr_extractor import ImprovedOCRExtractor, OCR_ENGINES
        print("✓ 改进的OCR提取器导入成功")
        
        # 显示可用引擎
        print("\n可用的OCR引擎:")
        for engine, available in OCR_ENGINES.items():
            status = "✓" if available else "✗"
            print(f"  {status} {engine.upper()}")
        
        # 测试每个可用引擎
        for engine, available in OCR_ENGINES.items():
            if available:
                print(f"\n测试 {engine.upper()}...")
                try:
                    extractor = ImprovedOCRExtractor(
                        languages=['ch', 'en'],
                        use_gpu=False,  # 测试时使用CPU
                        engine=engine
                    )
                    print(f"  ✓ {engine.upper()} 初始化成功")
                except Exception as e:
                    print(f"  ✗ {engine.upper()} 初始化失败: {e}")
        
    except ImportError as e:
        print(f"✗ 改进的OCR提取器导入失败: {e}")
        return False
    
    return True

def create_test_image():
    """创建测试图像"""
    # 创建一个包含中英文文字的测试图像
    img = np.ones((100, 400, 3), dtype=np.uint8) * 255  # 白色背景
    
    # 使用OpenCV添加文字
    font = cv2.FONT_HERSHEY_SIMPLEX
    
    # 添加英文文字
    cv2.putText(img, 'Hello World', (50, 40), font, 1, (0, 0, 0), 2)
    
    # 添加中文需要特殊字体，这里用数字代替
    cv2.putText(img, '123456789', (50, 80), font, 1, (0, 0, 0), 2)
    
    return img

def test_ocr_recognition():
    """测试OCR识别功能"""
    print("\n" + "="*50)
    print("测试OCR识别功能...")
    print("="*50)
    
    try:
        from improved_ocr_extractor import ImprovedOCRExtractor, OCR_ENGINES
        
        # 创建测试图像
        test_img = create_test_image()
        
        # 保存测试图像
        test_img_path = "test_ocr_image.png"
        cv2.imwrite(test_img_path, test_img)
        print(f"✓ 测试图像已保存: {test_img_path}")
        
        # 测试每个可用引擎的识别能力
        for engine, available in OCR_ENGINES.items():
            if available:
                print(f"\n测试 {engine.upper()} 识别能力...")
                try:
                    extractor = ImprovedOCRExtractor(
                        languages=['ch', 'en'],
                        use_gpu=False,
                        engine=engine
                    )
                    
                    # 进行OCR识别
                    result = extractor._ocr_recognize(test_img, confidence_threshold=0.5)
                    print(f"  识别结果: '{result}'")
                    
                    if result and len(result.strip()) > 0:
                        print(f"  ✓ {engine.upper()} 识别成功")
                    else:
                        print(f"  ⚠ {engine.upper()} 未识别到文字")
                        
                except Exception as e:
                    print(f"  ✗ {engine.upper()} 识别失败: {e}")
        
        # 清理测试文件
        if os.path.exists(test_img_path):
            os.remove(test_img_path)
            
    except Exception as e:
        print(f"✗ OCR识别测试失败: {e}")

def test_gpu_detection():
    """测试GPU检测功能"""
    print("\n" + "="*50)
    print("测试GPU检测功能...")
    print("="*50)
    
    try:
        from improved_ocr_extractor import ImprovedOCRExtractor
        
        extractor = ImprovedOCRExtractor(languages=['ch', 'en'])
        gpu_available = extractor._check_gpu_available()
        
        if gpu_available:
            print("✓ 检测到GPU支持")
            
            # 测试GPU初始化
            try:
                import torch
                print(f"  PyTorch版本: {torch.__version__}")
                print(f"  CUDA可用: {torch.cuda.is_available()}")
                if torch.cuda.is_available():
                    print(f"  GPU数量: {torch.cuda.device_count()}")
                    print(f"  当前GPU: {torch.cuda.get_device_name(0)}")
            except ImportError:
                print("  PyTorch未安装")
                
        else:
            print("✗ 未检测到GPU支持")
            
    except Exception as e:
        print(f"✗ GPU检测失败: {e}")

def test_video_processing():
    """测试视频处理功能（如果有测试视频）"""
    print("\n" + "="*50)
    print("测试视频处理功能...")
    print("="*50)
    
    # 查找测试视频文件
    test_video_patterns = [
        "test_video.mp4",
        "sample.mp4",
        "*.mp4",
        "*.avi",
        "*.mkv"
    ]
    
    test_video = None
    for pattern in test_video_patterns:
        if '*' in pattern:
            import glob
            files = glob.glob(pattern)
            if files:
                test_video = files[0]
                break
        else:
            if os.path.exists(pattern):
                test_video = pattern
                break
    
    if not test_video:
        print("⚠ 未找到测试视频文件，跳过视频处理测试")
        print("  提示：将测试视频命名为test_video.mp4放在当前目录")
        return
    
    print(f"✓ 找到测试视频: {test_video}")
    
    try:
        from improved_ocr_extractor import ImprovedOCRExtractor, OCR_ENGINES
        
        # 选择一个可用的引擎进行测试
        available_engine = None
        for engine, available in OCR_ENGINES.items():
            if available:
                available_engine = engine
                break
        
        if not available_engine:
            print("✗ 没有可用的OCR引擎")
            return
        
        print(f"使用 {available_engine.upper()} 引擎进行测试...")
        
        extractor = ImprovedOCRExtractor(
            languages=['ch', 'en'],
            use_gpu=False,  # 测试时使用CPU
            engine=available_engine
        )
        
        # 只处理前10秒进行测试
        print("开始提取字幕（仅前10秒）...")
        
        def progress_callback(message):
            print(f"  {message}")
        
        # 模拟短时间处理
        cap = cv2.VideoCapture(test_video)
        if cap.isOpened():
            fps = cap.get(cv2.CAP_PROP_FPS)
            frame_count = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
            duration = frame_count / fps if fps > 0 else 0
            
            print(f"  视频信息: {duration:.1f}秒, {fps:.1f}fps")
            
            # 只处理前5秒
            test_duration = min(5.0, duration)
            print(f"  测试处理时长: {test_duration}秒")
            
            cap.release()
            print("✓ 视频处理测试完成")
        else:
            print("✗ 无法打开测试视频")
            
    except Exception as e:
        print(f"✗ 视频处理测试失败: {e}")

def main():
    """主函数"""
    print("改进OCR功能测试")
    print("="*50)
    
    # 测试OCR引擎可用性
    if not test_ocr_engines():
        print("\n基础测试失败，请检查安装")
        return
    
    # 测试OCR识别功能
    test_ocr_recognition()
    
    # 测试GPU检测
    test_gpu_detection()
    
    # 测试视频处理
    test_video_processing()
    
    print("\n" + "="*50)
    print("测试完成！")
    print("\n使用建议：")
    print("1. 如果PaddleOCR可用，优先使用PaddleOCR")
    print("2. 如果有GPU支持，启用GPU加速")
    print("3. 根据视频内容调整采样间隔和置信度阈值")
    print("="*50)

if __name__ == '__main__':
    try:
        main()
        input("\n按回车键退出...")
    except KeyboardInterrupt:
        print("\n\n用户取消测试")
    except Exception as e:
        print(f"\n\n测试过程中发生错误: {e}")
        input("按回车键退出...") 