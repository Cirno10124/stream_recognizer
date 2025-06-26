#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
简单测试修复后的参数
"""

import sys
import os
import cv2
import numpy as np
from pathlib import Path

# 添加路径
current_dir = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, current_dir)

def check_paddleocr():
    """检查PaddleOCR安装和初始化"""
    try:
        from paddleocr import PaddleOCR
        
        # 检查CUDA
        try:
            import paddle
            cuda_available = paddle.device.is_compiled_with_cuda()
            gpu_count = paddle.device.cuda.device_count()
            print(f"CUDA可用: {cuda_available}, GPU数量: {gpu_count}")
        except:
            cuda_available = False
            gpu_count = 0
        
        # 选择设备
        device = 'gpu' if cuda_available and gpu_count > 0 else 'cpu'
        print(f"使用设备: {device}")
        
        # 初始化PaddleOCR - 仅使用兼容参数
        print("正在初始化PaddleOCR...")
        ocr = PaddleOCR(
            use_textline_orientation=True,
            lang='ch',
            device=device
        )
        
        print("✅ PaddleOCR初始化成功!")
        return ocr
        
    except Exception as e:
        print(f"❌ PaddleOCR初始化失败: {e}")
        return None

def extract_frame_region(video_path, frame_number=100):
    """提取视频帧的字幕区域"""
    cap = cv2.VideoCapture(video_path)
    if not cap.isOpened():
        print(f"❌ 无法打开视频: {video_path}")
        return None
    
    # 获取视频信息
    fps = cap.get(cv2.CAP_PROP_FPS)
    total_frames = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
    width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
    height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
    
    print(f"视频信息: {width}x{height}, {fps:.1f}fps, {total_frames}帧")
    
    # 跳到指定帧
    cap.set(cv2.CAP_PROP_POS_FRAMES, frame_number)
    ret, frame = cap.read()
    cap.release()
    
    if not ret:
        print(f"❌ 无法读取第{frame_number}帧")
        return None
    
    # 提取下30%区域作为字幕区域
    subtitle_y_start = int(height * 0.7)
    subtitle_region = frame[subtitle_y_start:height, 0:width]
    
    print(f"字幕区域尺寸: {subtitle_region.shape}")
    
    # 保存原始区域
    cv2.imwrite("test_subtitle_region.png", subtitle_region)
    print("已保存字幕区域: test_subtitle_region.png")
    
    return subtitle_region

def preprocess_for_ocr(image):
    """预处理图像"""
    # 转灰度
    if len(image.shape) == 3:
        gray = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY)
    else:
        gray = image
    
    # 轻微增强对比度
    clahe = cv2.createCLAHE(clipLimit=2.0, tileGridSize=(8,8))
    enhanced = clahe.apply(gray)
    
    # 保存处理后的图像
    cv2.imwrite("test_processed_region.png", enhanced)
    print("已保存处理后图像: test_processed_region.png")
    
    return enhanced

def test_ocr_recognition(ocr, image, confidence_threshold=0.1):
    """测试OCR识别"""
    print(f"\n开始OCR识别 (置信度阈值: {confidence_threshold})...")
    
    try:
        results = ocr.ocr(image)
        
        if results and results[0] is not None:
            print(f"识别到 {len(results[0])} 个文本块")
            texts = []
            
            for i, line in enumerate(results[0]):
                if line and len(line) >= 2:
                    bbox = line[0]
                    text_info = line[1]
                    if isinstance(text_info, (list, tuple)) and len(text_info) >= 2:
                        text, confidence = text_info[0], text_info[1]
                        print(f"  {i+1}. 文本: '{text}' | 置信度: {confidence:.3f}")
                        
                        if confidence >= confidence_threshold:
                            texts.append(text)
            
            final_text = ' '.join(texts)
            print(f"\n最终识别结果: '{final_text}'")
            return final_text
        else:
            print("❌ 未识别到任何文本")
            return ""
            
    except Exception as e:
        print(f"❌ OCR识别失败: {e}")
        return ""

def main():
    """主函数"""
    if len(sys.argv) != 2:
        print("用法: python simple_test_fix.py <视频文件路径>")
        return
    
    video_path = sys.argv[1]
    if not os.path.exists(video_path):
        print(f"❌ 视频文件不存在: {video_path}")
        return
    
    print(f"🎬 测试视频: {Path(video_path).name}")
    
    # 1. 检查PaddleOCR
    ocr = check_paddleocr()
    if not ocr:
        return
    
    # 2. 提取字幕区域
    subtitle_region = extract_frame_region(video_path, frame_number=500)  # 测试第500帧
    if subtitle_region is None:
        return
    
    # 3. 预处理
    processed_image = preprocess_for_ocr(subtitle_region)
    
    # 4. 测试不同置信度阈值
    confidence_thresholds = [0.05, 0.1, 0.2, 0.3]
    
    for threshold in confidence_thresholds:
        print(f"\n{'='*50}")
        result = test_ocr_recognition(ocr, processed_image, threshold)
        if result:
            print(f"✅ 置信度 {threshold}: 成功识别到文本!")
            break
        else:
            print(f"❌ 置信度 {threshold}: 未识别到文本")
    
    print(f"\n🎯 测试完成！检查以下文件:")
    print("  - test_subtitle_region.png (原始字幕区域)")
    print("  - test_processed_region.png (处理后的图像)")

if __name__ == "__main__":
    main() 