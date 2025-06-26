#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
基于Tesseract的字幕提取工具 - 避开PaddleOCR的PyTorch依赖问题
专门针对黑底白字字幕优化
"""

import os
import sys
import cv2
import numpy as np
import pytesseract
from typing import List, Dict, Tuple, Optional
import json
import re

class TesseractSubtitleExtractor:
    """基于Tesseract的字幕提取器"""
    
    def __init__(self, languages=['chi_sim', 'eng']):
        """初始化提取器"""
        self.languages = languages
        self.lang_string = '+'.join(languages)
        
        # Tesseract配置
        self.tesseract_config = '--psm 8 --oem 3'  # 单行文本识别
        
        print(f"初始化Tesseract字幕提取器")
        print(f"语言: {self.lang_string}")
        
        # 测试Tesseract是否可用
        try:
            test_img = np.ones((50, 200, 3), dtype=np.uint8) * 255
            cv2.putText(test_img, 'Test', (10, 30), cv2.FONT_HERSHEY_SIMPLEX, 1, (0, 0, 0), 2)
            result = pytesseract.image_to_string(test_img, lang=self.lang_string)
            print("✅ Tesseract初始化成功")
        except Exception as e:
            print(f"❌ Tesseract初始化失败: {e}")
            raise
    
    def extract_subtitles_from_video(self, video_path: str, 
                                   sample_interval: float = 2.0,
                                   subtitle_region: Optional[Tuple[int, int, int, int]] = None,
                                   preprocess_method: str = 'adaptive',
                                   confidence_threshold: float = 0.1,
                                   progress_callback: Optional[callable] = None) -> List[Dict]:
        """从视频中提取字幕"""
        
        print(f"开始提取字幕: {video_path}")
        print(f"采样间隔: {sample_interval}s, 预处理: {preprocess_method}")
        
        cap = cv2.VideoCapture(video_path)
        if not cap.isOpened():
            raise ValueError(f"无法打开视频文件: {video_path}")
        
        # 获取视频信息
        width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
        height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
        fps = cap.get(cv2.CAP_PROP_FPS)
        total_frames = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
        duration = total_frames / fps
        
        print(f"视频信息: {width}x{height}, {fps:.1f}fps, 时长: {duration:.1f}s")
        
        # 设置字幕区域
        if subtitle_region is None:
            # 默认使用底部30%区域
            subtitle_y_start = int(height * 0.7)
            subtitle_region = (0, subtitle_y_start, width, height - subtitle_y_start)
        
        x, y, w, h = subtitle_region
        print(f"字幕区域: ({x}, {y}, {w}, {h}) - 尺寸: {w}x{h}px")
        
        subtitles = []
        frame_interval = int(fps * sample_interval)
        total_samples = int(duration / sample_interval)
        
        print(f"预计采样 {total_samples} 个时间点，每 {frame_interval} 帧采样一次")
        
        current_frame = 0
        sample_count = 0
        
        while current_frame < total_frames:
            # 设置帧位置
            cap.set(cv2.CAP_PROP_POS_FRAMES, current_frame)
            ret, frame = cap.read()
            
            if not ret:
                break
            
            timestamp = current_frame / fps
            
            # 提取字幕区域
            subtitle_img = frame[y:y+h, x:x+w]
            
            # 保存调试图像（仅前几帧）
            if sample_count < 5:
                cv2.imwrite(f'debug_original_{sample_count+1}.png', subtitle_img)
            
            # 预处理图像
            processed_img = self._preprocess_for_black_bg_white_text(subtitle_img, method=preprocess_method)
            
            # 保存预处理后的调试图像
            if sample_count < 5:
                cv2.imwrite(f'debug_processed_{sample_count+1}.png', processed_img)
            
            # OCR识别
            text = self._ocr_recognize(processed_img)
            cleaned_text = self._clean_text(text)
            
            if cleaned_text:
                subtitles.append({
                    'timestamp': timestamp,
                    'text': cleaned_text,
                    'confidence': 1.0  # Tesseract不提供置信度，设为1.0
                })
                print(f"[{timestamp:.1f}s] {cleaned_text}")
            
            # 进度回调
            if progress_callback:
                progress = (sample_count + 1) / total_samples
                progress_callback(progress)
            
            sample_count += 1
            current_frame += frame_interval
        
        cap.release()
        
        print(f"提取完成！共找到 {len(subtitles)} 条字幕")
        return subtitles
    
    def _preprocess_for_black_bg_white_text(self, image: np.ndarray, method: str = 'adaptive') -> np.ndarray:
        """专门针对黑底白字字幕的预处理"""
        
        if method == 'none':
            return image
        
        # 转换为灰度图
        if len(image.shape) == 3:
            gray = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY)
        else:
            gray = image.copy()
        
        if method == 'simple':
            # 简单处理：只增强对比度
            clahe = cv2.createCLAHE(clipLimit=2.0, tileGridSize=(8,8))
            enhanced = clahe.apply(gray)
            return enhanced
        
        elif method == 'adaptive':
            # 自适应处理：根据图像特征选择最佳处理方式
            
            # 分析图像特征
            mean_brightness = np.mean(gray)
            std_brightness = np.std(gray)
            
            if mean_brightness < 100 and std_brightness > 40:
                # 典型的黑底白字：低平均亮度，高标准差
                
                # 1. 轻微去噪
                denoised = cv2.bilateralFilter(gray, 5, 50, 50)
                
                # 2. 增强对比度
                clahe = cv2.createCLAHE(clipLimit=3.0, tileGridSize=(8,8))
                enhanced = clahe.apply(denoised)
                
                # 3. 锐化边缘
                kernel = np.array([[-1,-1,-1], [-1,9,-1], [-1,-1,-1]])
                sharpened = cv2.filter2D(enhanced, -1, kernel)
                
                # 4. 放大图像提高识别率
                scale_factor = 2
                h, w = sharpened.shape
                enlarged = cv2.resize(sharpened, (w * scale_factor, h * scale_factor), 
                                    interpolation=cv2.INTER_CUBIC)
                
                return enlarged
            
            else:
                # 其他情况：使用通用处理
                clahe = cv2.createCLAHE(clipLimit=2.0, tileGridSize=(8,8))
                enhanced = clahe.apply(gray)
                return enhanced
        
        elif method == 'aggressive':
            # 激进处理：包括二值化
            
            # 1. 去噪
            denoised = cv2.bilateralFilter(gray, 9, 75, 75)
            
            # 2. 增强对比度
            clahe = cv2.createCLAHE(clipLimit=4.0, tileGridSize=(8,8))
            enhanced = clahe.apply(denoised)
            
            # 3. 自适应二值化
            binary = cv2.adaptiveThreshold(enhanced, 255, cv2.ADAPTIVE_THRESH_GAUSSIAN_C, 
                                         cv2.THRESH_BINARY, 11, 2)
            
            # 4. 形态学操作
            kernel = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (2, 2))
            cleaned = cv2.morphologyEx(binary, cv2.MORPH_CLOSE, kernel)
            
            # 5. 放大
            scale_factor = 3
            h, w = cleaned.shape
            enlarged = cv2.resize(cleaned, (w * scale_factor, h * scale_factor), 
                                interpolation=cv2.INTER_CUBIC)
            
            return enlarged
        
        return gray
    
    def _ocr_recognize(self, image: np.ndarray) -> str:
        """使用Tesseract进行OCR识别"""
        try:
            # 使用优化的配置进行识别
            text = pytesseract.image_to_string(
                image, 
                lang=self.lang_string,
                config=self.tesseract_config
            )
            return text.strip()
        except Exception as e:
            return ""
    
    def _clean_text(self, text: str) -> str:
        """清理识别到的文本"""
        if not text:
            return ""
        
        # 移除多余的空白字符
        cleaned = re.sub(r'\s+', ' ', text.strip())
        
        # 移除单个字符（通常是噪音）
        if len(cleaned) <= 1:
            return ""
        
        # 移除纯符号文本
        if re.match(r'^[^\w\u4e00-\u9fff]+$', cleaned):
            return ""
        
        return cleaned

def main():
    """主函数"""
    import argparse
    
    parser = argparse.ArgumentParser(description='基于Tesseract的字幕提取工具')
    parser.add_argument('video_path', help='视频文件路径')
    parser.add_argument('--output', '-o', help='输出文件路径')
    parser.add_argument('--interval', type=float, default=2.0, help='采样间隔（秒）')
    parser.add_argument('--preprocess', choices=['none', 'simple', 'adaptive', 'aggressive'], 
                       default='adaptive', help='预处理方法')
    parser.add_argument('--languages', nargs='+', default=['chi_sim', 'eng'], 
                       help='识别语言')
    
    args = parser.parse_args()
    
    if not os.path.exists(args.video_path):
        print(f"❌ 视频文件不存在: {args.video_path}")
        return
    
    # 设置输出路径
    if args.output:
        output_path = args.output
    else:
        base_name = os.path.splitext(os.path.basename(args.video_path))[0]
        output_path = f"{base_name}_subtitles_tesseract.json"
    
    try:
        # 创建提取器
        extractor = TesseractSubtitleExtractor(languages=args.languages)
        
        # 提供进度回调
        def progress_callback(progress):
            percent = int(progress * 100)
            print(f"\r处理进度: {percent}%", end='', flush=True)
        
        # 提取字幕
        subtitles = extractor.extract_subtitles_from_video(
            args.video_path,
            sample_interval=args.interval,
            preprocess_method=args.preprocess,
            progress_callback=progress_callback
        )
        
        print()  # 换行
        
        # 保存结果
        with open(output_path, 'w', encoding='utf-8') as f:
            json.dump(subtitles, f, ensure_ascii=False, indent=2)
        print(f"结果已保存到: {output_path}")
        
        if subtitles:
            print(f"\n✅ 成功提取 {len(subtitles)} 条字幕")
            print("前几条字幕:")
            for i, subtitle in enumerate(subtitles[:5]):
                print(f"  {subtitle['timestamp']:.1f}s: {subtitle['text']}")
        else:
            print("\n❌ 未提取到字幕")
            print("建议尝试:")
            print("1. 不同的预处理方法: --preprocess aggressive")
            print("2. 更短的采样间隔: --interval 1.0")
            print("3. 检查保存的调试图像")
        
    except Exception as e:
        print(f"\n❌ 处理失败: {e}")
        import traceback
        traceback.print_exc()

if __name__ == "__main__":
    main() 