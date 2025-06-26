#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
详细的OCR调试脚本 - 帮助排查字幕识别问题
"""

import os
import sys
import cv2
import numpy as np

# 添加当前目录到路径
current_dir = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, current_dir)

def debug_ocr_step_by_step(video_path):
    """逐步调试OCR识别过程"""
    try:
        from tools.ocr.improved_ocr_extractor import ImprovedOCRExtractor
        
        print("=== 详细OCR调试 ===\n")
        
        # 1. 初始化OCR
        print("1. 初始化OCR提取器...")
        extractor = ImprovedOCRExtractor(
            languages=['ch', 'en'],
            use_gpu=False,
            engine='paddleocr'
        )
        print(f"✅ OCR引擎: {extractor.engine_name}")
        
        # 2. 打开视频
        print(f"\n2. 打开视频: {video_path}")
        cap = cv2.VideoCapture(video_path)
        if not cap.isOpened():
            print(f"❌ 无法打开视频文件")
            return False
        
        # 获取视频信息
        width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
        height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
        fps = cap.get(cv2.CAP_PROP_FPS)
        total_frames = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
        
        print(f"✅ 视频信息: {width}x{height}, {fps:.1f}fps, {total_frames}帧")
        
        # 3. 测试多个字幕区域
        regions = [
            ("底部30%", 0, int(height * 0.7), width, height),
            ("底部20%", 0, int(height * 0.8), width, height),
            ("底部40%", 0, int(height * 0.6), width, height),
            ("全屏", 0, 0, width, height)
        ]
        
        # 4. 测试几个不同的帧
        test_frames = [30, 60, 120, 180, 300]
        
        for frame_idx, frame_num in enumerate(test_frames):
            if frame_num >= total_frames:
                continue
                
            print(f"\n=== 测试帧 {frame_num} (时间: {frame_num/fps:.1f}s) ===")
            
            # 跳转到指定帧
            cap.set(cv2.CAP_PROP_POS_FRAMES, frame_num)
            ret, frame = cap.read()
            
            if not ret:
                print("❌ 无法读取帧")
                continue
            
            # 测试不同的字幕区域
            for region_name, x1, y1, x2, y2 in regions:
                print(f"\n--- 区域: {region_name} ({x1},{y1},{x2},{y2}) ---")
                
                region_img = frame[y1:y2, x1:x2]
                
                # 保存原始区域
                original_path = f"debug_f{frame_idx+1}_{region_name.replace('%', 'p')}_original.png"
                cv2.imwrite(original_path, region_img)
                print(f"💾 原始区域: {original_path}")
                
                # 测试不同预处理方法
                methods = ['none', 'minimal']
                
                for method in methods:
                    print(f"  🔄 预处理: {method}")
                    
                    if method == 'none':
                        processed_img = region_img
                    else:
                        processed_img = extractor._preprocess_image(region_img, method=method)
                    
                    # 保存预处理结果
                    processed_path = f"debug_f{frame_idx+1}_{region_name.replace('%', 'p')}_{method}.png"
                    cv2.imwrite(processed_path, processed_img)
                    
                    # OCR识别 - 使用极低置信度
                    try:
                        text = extractor._ocr_recognize(processed_img, confidence_threshold=0.05)
                        cleaned_text = extractor._clean_text(text)
                        
                        print(f"    🔍 原始: '{text}'")
                        print(f"    ✨ 清理: '{cleaned_text}'")
                        
                        if cleaned_text:
                            print(f"    ✅ 成功! 区域:{region_name}, 预处理:{method}")
                            
                    except Exception as e:
                        print(f"    ❌ OCR错误: {e}")
        
        cap.release()
        return True
        
    except Exception as e:
        print(f"❌ 调试失败: {e}")
        import traceback
        traceback.print_exc()
        return False

def test_paddleocr_basic():
    """测试PaddleOCR基本功能"""
    try:
        print("\n=== PaddleOCR基本测试 ===")
        
        from paddleocr import PaddleOCR
        
        # 测试多种语言配置
        configs = [
            ('ch', '中文'),
            ('en', '英文'),
            (['ch', 'en'], '中英文混合')
        ]
        
        for lang, desc in configs:
            print(f"\n--- 测试语言: {desc} ---")
            try:
                ocr = PaddleOCR(use_textline_orientation=True, lang=lang, device='cpu')
                
                # 创建测试图像
                test_img = np.ones((100, 400, 3), dtype=np.uint8) * 255
                cv2.putText(test_img, 'Hello 你好', (20, 60),
                           cv2.FONT_HERSHEY_SIMPLEX, 1.5, (0, 0, 0), 2)
                
                # OCR识别
                results = ocr.ocr(test_img)
                print(f"✅ {desc} 配置成功")
                
                if results and results[0]:
                    for line in results[0]:
                        if line and len(line) >= 2:
                            text_info = line[1]
                            if isinstance(text_info, (list, tuple)) and len(text_info) >= 2:
                                text, confidence = text_info[0], text_info[1]
                                print(f"    识别: '{text}' (置信度: {confidence:.3f})")
                
            except Exception as e:
                print(f"❌ {desc} 配置失败: {e}")
        
        return True
        
    except Exception as e:
        print(f"❌ PaddleOCR测试失败: {e}")
        return False

def main():
    """主函数"""
    print("=== OCR详细调试工具 ===")
    
    # 先测试PaddleOCR基本功能
    test_paddleocr_basic()
    
    # 然后测试视频OCR
    if len(sys.argv) > 1:
        video_path = sys.argv[1]
        if os.path.exists(video_path):
            print(f"\n开始调试视频: {video_path}")
            debug_ocr_step_by_step(video_path)
        else:
            print(f"❌ 视频文件不存在: {video_path}")
    else:
        print("\n使用方法:")
        print("python debug_ocr_detailed.py <视频文件路径>")
        print("或者将视频文件拖拽到此脚本上")

if __name__ == "__main__":
    main() 