#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
快速测试黑底白字字幕识别
"""

import os
import sys
import cv2
import numpy as np

# 添加当前目录到路径
current_dir = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, current_dir)

def quick_test_black_bg_subtitle(video_path):
    """快速测试黑底白字字幕识别"""
    try:
        from tools.ocr.improved_ocr_extractor import ImprovedOCRExtractor
        
        print("=== 快速测试黑底白字字幕 ===\n")
        
        # 初始化OCR - 专门针对黑底白字优化
        print("初始化OCR...")
        extractor = ImprovedOCRExtractor(
            languages=['ch', 'en'],  # 中英文混合
            use_gpu=False,
            engine='paddleocr'
        )
        
        # 打开视频
        cap = cv2.VideoCapture(video_path)
        if not cap.isOpened():
            print(f"❌ 无法打开视频: {video_path}")
            return False
        
        # 获取视频信息
        width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
        height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
        fps = cap.get(cv2.CAP_PROP_FPS)
        total_frames = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
        
        print(f"视频信息: {width}x{height}, {fps:.1f}fps")
        
        # 设置多个字幕区域进行测试
        regions = [
            ("底部25%", int(height * 0.75), int(height * 0.25)),
            ("底部30%", int(height * 0.7), int(height * 0.3)),
            ("底部35%", int(height * 0.65), int(height * 0.35)),
        ]
        
        # 测试多个时间点
        test_times = [10, 30, 60, 120, 180]  # 秒
        
        results_found = False
        
        for time_sec in test_times:
            frame_num = int(time_sec * fps)
            if frame_num >= total_frames:
                continue
            
            print(f"\n--- 测试时间点: {time_sec}s ---")
            
            # 跳转到指定帧
            cap.set(cv2.CAP_PROP_POS_FRAMES, frame_num)
            ret, frame = cap.read()
            
            if not ret:
                continue
            
            # 测试不同区域
            for region_name, y_start, region_height in regions:
                y_end = y_start + region_height
                if y_end > height:
                    y_end = height
                
                # 提取字幕区域
                subtitle_region = frame[y_start:y_end, 0:width]
                
                print(f"  区域 {region_name}: {subtitle_region.shape}")
                
                # 保存原始区域用于检查
                region_path = f"test_{time_sec}s_{region_name.replace('%', 'p')}_original.png"
                cv2.imwrite(region_path, subtitle_region)
                
                # 测试不同的预处理方法，重点测试对黑底白字有效的方法
                methods = ['none', 'minimal']
                
                for method in methods:
                    print(f"    预处理: {method}")
                    
                    # 预处理
                    if method == 'none':
                        processed = subtitle_region
                    else:
                        processed = extractor._preprocess_image(subtitle_region, method=method)
                    
                    # 保存预处理结果
                    processed_path = f"test_{time_sec}s_{region_name.replace('%', 'p')}_{method}.png"
                    cv2.imwrite(processed_path, processed)
                    
                    # OCR识别 - 使用非常低的置信度
                    confidence_levels = [0.1, 0.2, 0.3]
                    
                    for conf in confidence_levels:
                        try:
                            text = extractor._ocr_recognize(processed, confidence_threshold=conf)
                            cleaned = extractor._clean_text(text)
                            
                            if cleaned:
                                print(f"      ✅ 置信度{conf}: '{cleaned}'")
                                results_found = True
                                # 找到结果就继续下一个方法
                                break
                            else:
                                print(f"      ❌ 置信度{conf}: 无结果")
                        except Exception as e:
                            print(f"      ❌ 置信度{conf}: 错误 {e}")
        
        cap.release()
        
        if results_found:
            print("\n✅ 测试成功！找到了字幕内容")
            print("建议使用找到结果的配置重新运行完整提取")
        else:
            print("\n❌ 测试失败，未找到字幕内容")
            print("可能的原因：")
            print("1. 字幕区域设置不正确")
            print("2. 字幕对比度不够")
            print("3. 字幕字体太小或太模糊")
            print("4. 语言设置不匹配")
            
        print(f"\n检查保存的图像文件:")
        print("- test_*_original.png: 原始字幕区域")
        print("- test_*_none.png: 无预处理")
        print("- test_*_minimal.png: 最简预处理")
        
        return results_found
        
    except Exception as e:
        print(f"❌ 测试失败: {e}")
        import traceback
        traceback.print_exc()
        return False

def create_test_image():
    """创建测试用的黑底白字图像"""
    # 创建黑色背景
    img = np.zeros((100, 600, 3), dtype=np.uint8)
    
    # 添加白色字幕文字
    cv2.putText(img, 'This is a test subtitle', (50, 40), 
                cv2.FONT_HERSHEY_SIMPLEX, 1, (255, 255, 255), 2)
    cv2.putText(img, '这是测试字幕', (50, 80), 
                cv2.FONT_HERSHEY_SIMPLEX, 1, (255, 255, 255), 2)
    
    # 保存测试图像
    cv2.imwrite('black_bg_test.png', img)
    print("已创建测试图像: black_bg_test.png")
    
    return img

def test_with_image():
    """用测试图像测试OCR"""
    try:
        from tools.ocr.improved_ocr_extractor import ImprovedOCRExtractor
        
        print("\n=== 测试图像OCR ===")
        
        # 创建测试图像
        test_img = create_test_image()
        
        # 初始化OCR
        extractor = ImprovedOCRExtractor(
            languages=['ch', 'en'],
            use_gpu=False,
            engine='paddleocr'
        )
        
        # 测试不同的预处理
        methods = ['none', 'minimal']
        
        for method in methods:
            print(f"\n测试预处理方法: {method}")
            
            if method == 'none':
                processed = test_img
            else:
                processed = extractor._preprocess_image(test_img, method=method)
            
            # 保存处理结果
            cv2.imwrite(f'test_image_{method}.png', processed)
            
            # OCR识别
            text = extractor._ocr_recognize(processed, confidence_threshold=0.1)
            cleaned = extractor._clean_text(text)
            
            print(f"  原始结果: '{text}'")
            print(f"  清理结果: '{cleaned}'")
            
            if cleaned:
                print(f"  ✅ 成功识别!")
            else:
                print(f"  ❌ 未识别到文本")
                
    except Exception as e:
        print(f"❌ 图像测试失败: {e}")

def main():
    print("=== 黑底白字字幕快速测试工具 ===\n")
    
    # 先用测试图像验证OCR基本功能
    test_with_image()
    
    # 然后测试视频
    if len(sys.argv) > 1:
        video_path = sys.argv[1]
        if os.path.exists(video_path):
            print(f"\n开始测试视频: {video_path}")
            success = quick_test_black_bg_subtitle(video_path)
            
            if success:
                print("\n建议运行完整提取:")
                print(f"python start_subtitle_wer_gui.py")
                print("或者")
                print(f"python tools/ocr/improved_ocr_extractor.py \"{video_path}\" --confidence 0.2 --interval 2")
        else:
            print(f"❌ 视频文件不存在: {video_path}")
    else:
        print("使用方法: python quick_test_black_bg.py <视频文件路径>")

if __name__ == "__main__":
    main() 