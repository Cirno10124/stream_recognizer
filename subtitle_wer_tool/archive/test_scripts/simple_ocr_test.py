#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
简单OCR测试 - 避开PyTorch依赖问题
"""

import os
import sys
import cv2
import numpy as np

def test_basic_ocr():
    """测试基本OCR功能，避开复杂依赖"""
    print("=== 简单OCR测试 ===\n")
    
    # 创建测试图像（黑底白字）
    print("1. 创建黑底白字测试图像...")
    img = np.zeros((120, 800, 3), dtype=np.uint8)
    
    # 添加白色字幕文字（模拟真实字幕）
    cv2.putText(img, 'Hello World - Test Subtitle', (50, 50), 
                cv2.FONT_HERSHEY_SIMPLEX, 1.2, (255, 255, 255), 2)
    cv2.putText(img, '这是一个测试字幕', (50, 90), 
                cv2.FONT_HERSHEY_SIMPLEX, 1.2, (255, 255, 255), 2)
    
    cv2.imwrite('simple_test_image.png', img)
    print("✅ 测试图像已保存: simple_test_image.png")
    
    # 尝试不同的OCR引擎
    print("\n2. 测试OCR引擎...")
    
    # 测试 Tesseract（通常最稳定）
    try:
        import pytesseract
        print("🔄 测试Tesseract...")
        
        # 转换为灰度
        gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
        
        # 基本识别
        text = pytesseract.image_to_string(gray, lang='chi_sim+eng')
        print(f"  Tesseract结果: '{text.strip()}'")
        
        if text.strip():
            print("  ✅ Tesseract工作正常!")
            return True
        else:
            print("  ❌ Tesseract未识别到文本")
            
    except ImportError:
        print("  ❌ Tesseract未安装")
    except Exception as e:
        print(f"  ❌ Tesseract错误: {e}")
    
    # 测试简单的PaddleOCR（不加载复杂模型）
    try:
        print("🔄 测试PaddleOCR (CPU模式)...")
        
        # 设置环境变量，强制使用CPU
        os.environ['CUDA_VISIBLE_DEVICES'] = ''
        
        from paddleocr import PaddleOCR
        
        # 初始化为最简模式
        ocr = PaddleOCR(
            use_textline_orientation=False,  # 关闭文本方向检测
            lang='ch',
            device='cpu',
            use_mp=False,  # 关闭多进程
            show_log=False
        )
        
        results = ocr.ocr(img)
        print(f"  PaddleOCR结果: {results}")
        
        if results and results[0]:
            for line in results[0]:
                if line and len(line) >= 2:
                    text_info = line[1]
                    if isinstance(text_info, (list, tuple)) and len(text_info) >= 2:
                        text, confidence = text_info[0], text_info[1]
                        print(f"    识别: '{text}' (置信度: {confidence:.3f})")
            print("  ✅ PaddleOCR工作正常!")
            return True
        else:
            print("  ❌ PaddleOCR未识别到文本")
            
    except Exception as e:
        print(f"  ❌ PaddleOCR错误: {e}")
    
    print("\n❌ 所有OCR引擎都遇到问题")
    return False

def test_video_subtitle_region(video_path):
    """测试视频字幕区域提取"""
    print(f"\n3. 测试视频字幕区域: {video_path}")
    
    cap = cv2.VideoCapture(video_path)
    if not cap.isOpened():
        print("❌ 无法打开视频文件")
        return False
    
    # 获取视频信息
    width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
    height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
    fps = cap.get(cv2.CAP_PROP_FPS)
    total_frames = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
    
    print(f"视频信息: {width}x{height}, {fps:.1f}fps, 总帧数: {total_frames}")
    
    # 跳转到中间某个帧
    test_frame = min(300, total_frames // 2)
    cap.set(cv2.CAP_PROP_POS_FRAMES, test_frame)
    ret, frame = cap.read()
    
    if not ret:
        print("❌ 无法读取视频帧")
        cap.release()
        return False
    
    # 保存完整帧
    cv2.imwrite('video_full_frame.png', frame)
    print("💾 完整帧已保存: video_full_frame.png")
    
    # 提取不同的字幕区域
    regions = [
        ("底部20%", int(height * 0.8), height),
        ("底部30%", int(height * 0.7), height),
        ("底部40%", int(height * 0.6), height),
    ]
    
    for region_name, y_start, y_end in regions:
        subtitle_region = frame[y_start:y_end, 0:width]
        region_file = f"subtitle_{region_name.replace('%', 'p')}.png"
        cv2.imwrite(region_file, subtitle_region)
        print(f"💾 {region_name}区域已保存: {region_file} (尺寸: {subtitle_region.shape})")
    
    cap.release()
    
    print("\n✅ 请检查保存的图像文件:")
    print("- video_full_frame.png: 完整视频帧")
    print("- subtitle_*.png: 不同的字幕区域")
    print("- 查看这些图像，确认哪个区域包含清晰的字幕")
    
    return True

def check_dependencies():
    """检查依赖项"""
    print("=== 检查依赖项 ===")
    
    # 检查OpenCV
    try:
        print(f"✅ OpenCV版本: {cv2.__version__}")
    except:
        print("❌ OpenCV未安装")
    
    # 检查Tesseract
    try:
        import pytesseract
        print("✅ Tesseract可用")
    except ImportError:
        print("❌ Tesseract未安装")
    
    # 检查PaddleOCR（不实际加载）
    try:
        import paddleocr
        print("✅ PaddleOCR库已安装")
    except ImportError:
        print("❌ PaddleOCR未安装")
    
    print()

def main():
    print("=== 简单OCR诊断工具 ===\n")
    
    # 检查依赖
    check_dependencies()
    
    # 测试基本OCR功能
    ocr_works = test_basic_ocr()
    
    # 如果提供了视频路径，测试视频
    if len(sys.argv) > 1:
        video_path = sys.argv[1]
        if os.path.exists(video_path):
            test_video_subtitle_region(video_path)
        else:
            print(f"❌ 视频文件不存在: {video_path}")
    else:
        print("\n要测试视频字幕提取，请运行:")
        print("python simple_ocr_test.py <视频文件路径>")
    
    if ocr_works:
        print("\n✅ OCR基本功能正常，可以进行字幕提取")
    else:
        print("\n❌ OCR功能有问题，需要解决依赖问题")
        print("建议解决方案:")
        print("1. 安装Tesseract: https://github.com/tesseract-ocr/tesseract")
        print("2. 重新安装PaddleOCR: pip uninstall paddleocr && pip install paddleocr")

if __name__ == "__main__":
    main() 