#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
基于Tesseract的字幕提取工具 - 解决PaddleOCR的PyTorch依赖问题
"""

import os
import sys
import cv2
import numpy as np
import pytesseract
import json
import re

def extract_subtitles_with_tesseract(video_path, interval=2.0, preprocess='adaptive'):
    """使用Tesseract提取字幕"""
    
    print(f"=== 基于Tesseract的字幕提取 ===")
    print(f"视频: {video_path}")
    print(f"间隔: {interval}s, 预处理: {preprocess}")
    
    cap = cv2.VideoCapture(video_path)
    if not cap.isOpened():
        print("❌ 无法打开视频")
        return []
    
    # 视频信息
    width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
    height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
    fps = cap.get(cv2.CAP_PROP_FPS)
    total_frames = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
    
    print(f"视频信息: {width}x{height}, {fps:.1f}fps")
    
    # 字幕区域（底部30%）
    y_start = int(height * 0.7)
    subtitle_height = height - y_start
    
    print(f"字幕区域: (0, {y_start}, {width}, {subtitle_height})")
    
    subtitles = []
    frame_interval = int(fps * interval)
    current_frame = 0
    sample_count = 0
    
    while current_frame < total_frames:
        cap.set(cv2.CAP_PROP_POS_FRAMES, current_frame)
        ret, frame = cap.read()
        
        if not ret:
            break
        
        timestamp = current_frame / fps
        
        # 提取字幕区域
        subtitle_region = frame[y_start:, 0:width]
        
        # 保存调试图像（前几帧）
        if sample_count < 3:
            cv2.imwrite(f'tesseract_debug_{sample_count+1}_original.png', subtitle_region)
        
        # 预处理
        processed = preprocess_image(subtitle_region, method=preprocess)
        
        if sample_count < 3:
            cv2.imwrite(f'tesseract_debug_{sample_count+1}_processed.png', processed)
        
        # OCR识别
        try:
            text = pytesseract.image_to_string(processed, lang='chi_sim+eng', config='--psm 8')
            cleaned = clean_text(text)
            
            if cleaned:
                subtitles.append({
                    'timestamp': timestamp,
                    'text': cleaned
                })
                print(f"[{timestamp:.1f}s] {cleaned}")
        except Exception as e:
            pass  # 静默处理错误
        
        sample_count += 1
        current_frame += frame_interval
        
        # 显示进度
        if sample_count % 10 == 0:
            progress = current_frame / total_frames * 100
            print(f"进度: {progress:.1f}%")
    
    cap.release()
    return subtitles

def preprocess_image(image, method='adaptive'):
    """图像预处理"""
    
    # 转换为灰度
    if len(image.shape) == 3:
        gray = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY)
    else:
        gray = image.copy()
    
    if method == 'none':
        return gray
    
    elif method == 'simple':
        # 简单增强对比度
        clahe = cv2.createCLAHE(clipLimit=2.0, tileGridSize=(8,8))
        enhanced = clahe.apply(gray)
        return enhanced
    
    elif method == 'adaptive':
        # 自适应处理 - 专门针对黑底白字
        mean_val = np.mean(gray)
        
        if mean_val < 100:  # 检测到暗背景（可能是黑底白字）
            # 1. 轻微去噪
            denoised = cv2.bilateralFilter(gray, 5, 50, 50)
            
            # 2. 增强对比度
            clahe = cv2.createCLAHE(clipLimit=3.0, tileGridSize=(8,8))
            enhanced = clahe.apply(denoised)
            
            # 3. 轻微锐化
            kernel = np.array([[-1,-1,-1], [-1,9,-1], [-1,-1,-1]])
            sharpened = cv2.filter2D(enhanced, -1, kernel)
            
            # 4. 放大提高识别率
            h, w = sharpened.shape
            enlarged = cv2.resize(sharpened, (w*2, h*2), interpolation=cv2.INTER_CUBIC)
            
            return enlarged
        else:
            # 亮背景的通用处理
            clahe = cv2.createCLAHE(clipLimit=2.0, tileGridSize=(8,8))
            return clahe.apply(gray)
    
    elif method == 'aggressive':
        # 激进处理，包括二值化
        
        # 去噪
        denoised = cv2.bilateralFilter(gray, 9, 75, 75)
        
        # 增强对比度
        clahe = cv2.createCLAHE(clipLimit=4.0, tileGridSize=(8,8))
        enhanced = clahe.apply(denoised)
        
        # 自适应二值化
        binary = cv2.adaptiveThreshold(enhanced, 255, cv2.ADAPTIVE_THRESH_GAUSSIAN_C, 
                                     cv2.THRESH_BINARY, 11, 2)
        
        # 形态学操作
        kernel = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (2, 2))
        cleaned = cv2.morphologyEx(binary, cv2.MORPH_CLOSE, kernel)
        
        # 放大
        h, w = cleaned.shape
        enlarged = cv2.resize(cleaned, (w*3, h*3), interpolation=cv2.INTER_CUBIC)
        
        return enlarged
    
    return gray

def clean_text(text):
    """清理文本"""
    if not text:
        return ""
    
    # 移除多余空格
    cleaned = re.sub(r'\s+', ' ', text.strip())
    
    # 过滤太短的文本
    if len(cleaned) <= 1:
        return ""
    
    # 过滤纯符号
    if re.match(r'^[^\w\u4e00-\u9fff]+$', cleaned):
        return ""
    
    return cleaned

def main():
    """主函数"""
    import argparse
    
    parser = argparse.ArgumentParser(description='Tesseract字幕提取器')
    parser.add_argument('video_path', help='视频文件路径')
    parser.add_argument('--interval', type=float, default=2.0, help='采样间隔')
    parser.add_argument('--preprocess', choices=['none', 'simple', 'adaptive', 'aggressive'], 
                       default='adaptive', help='预处理方法')
    
    args = parser.parse_args()
    
    if not os.path.exists(args.video_path):
        print(f"❌ 文件不存在: {args.video_path}")
        return
    
    # 提取字幕
    subtitles = extract_subtitles_with_tesseract(
        args.video_path, 
        interval=args.interval,
        preprocess=args.preprocess
    )
    
    # 保存结果
    base_name = os.path.splitext(os.path.basename(args.video_path))[0]
    output_file = f"{base_name}_tesseract_subtitles.json"
    
    with open(output_file, 'w', encoding='utf-8') as f:
        json.dump(subtitles, f, ensure_ascii=False, indent=2)
    
    print(f"\n=== 提取完成 ===")
    print(f"找到字幕: {len(subtitles)} 条")
    print(f"结果保存: {output_file}")
    
    if len(subtitles) == 0:
        print("\n❌ 未找到字幕，建议尝试:")
        print("1. 不同预处理: --preprocess aggressive")
        print("2. 更短间隔: --interval 1.0")
        print("3. 检查调试图像文件")
    else:
        print("\n✅ 前几条字幕:")
        for i, sub in enumerate(subtitles[:5]):
            print(f"  {sub['timestamp']:.1f}s: {sub['text']}")

if __name__ == "__main__":
    main() 