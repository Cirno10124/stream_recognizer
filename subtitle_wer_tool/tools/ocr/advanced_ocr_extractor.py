#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
高级OCR字幕提取模块
集成多种OCR引擎，提供更好的字幕识别效果
"""

import os
import cv2
import numpy as np
import tempfile
import subprocess
from pathlib import Path
from typing import List, Tuple, Optional, Dict, Union
import json
import re
from datetime import datetime, timedelta
import gc
import time

# 检查可用的OCR引擎
OCR_ENGINES = {}

# PaddleOCR - 推荐，中文效果最好
try:
    from paddleocr import PaddleOCR
    OCR_ENGINES['paddleocr'] = True
except ImportError:
    OCR_ENGINES['paddleocr'] = False

# EasyOCR - 备选
try:
    import easyocr
    OCR_ENGINES['easyocr'] = True
except ImportError:
    OCR_ENGINES['easyocr'] = False

# Tesseract - 基础引擎
try:
    import pytesseract
    from PIL import Image
    OCR_ENGINES['tesseract'] = True
except ImportError:
    OCR_ENGINES['tesseract'] = False

# TrOCR - 微软的Transformer OCR
try:
    from transformers import TrOCRProcessor, VisionEncoderDecoderModel
    OCR_ENGINES['trocr'] = True
except ImportError:
    OCR_ENGINES['trocr'] = False


class AdvancedOCRExtractor:
    """高级OCR字幕提取器"""
    
    def __init__(self, 
                 languages: List[str] = None, 
                 use_gpu: bool = True,
                 engine: str = 'auto'):
        """
        初始化OCR提取器
        
        Args:
            languages: 识别语言列表
            use_gpu: 是否使用GPU加速
            engine: OCR引擎选择 ('auto', 'paddleocr', 'easyocr', 'tesseract', 'trocr')
        """
        self.languages = languages or ['ch', 'en']
        self.use_gpu = use_gpu
        self.engine_name = engine
        self.ocr_engine = None
        self.processor = None
        self.model = None
        
        # 初始化OCR引擎
        self._init_ocr_engine()
        
    def _init_ocr_engine(self):
        """初始化OCR引擎"""
        if self.engine_name == 'auto':
            # 自动选择最佳引擎
            if OCR_ENGINES.get('paddleocr', False):
                self.engine_name = 'paddleocr'
            elif OCR_ENGINES.get('easyocr', False):
                self.engine_name = 'easyocr'
            elif OCR_ENGINES.get('tesseract', False):
                self.engine_name = 'tesseract'
            else:
                raise RuntimeError("未找到可用的OCR引擎")
        
        print(f"初始化OCR引擎: {self.engine_name}")
        
        if self.engine_name == 'paddleocr':
            self._init_paddleocr()
        elif self.engine_name == 'easyocr':
            self._init_easyocr()
        elif self.engine_name == 'tesseract':
            self._init_tesseract()
        elif self.engine_name == 'trocr':
            self._init_trocr()
        else:
            raise ValueError(f"不支持的OCR引擎: {self.engine_name}")
    
    def _init_paddleocr(self):
        """初始化PaddleOCR"""
        if not OCR_ENGINES.get('paddleocr', False):
            raise RuntimeError("PaddleOCR未安装")
        
        try:
            # 检测GPU可用性
            gpu_available = self._check_gpu_available()
            use_gpu = self.use_gpu and gpu_available
            
            if self.use_gpu and not gpu_available:
                print("警告：请求使用GPU但未检测到CUDA支持，将使用CPU")
            
            # 语言映射
            lang_map = {
                'ch': 'ch',
                'ch_sim': 'ch',
                'en': 'en',
                'ja': 'japan',
                'ko': 'korean'
            }
            
            # PaddleOCR支持的语言
            ocr_lang = 'ch'  # 默认中文
            for lang in self.languages:
                if lang in lang_map:
                    ocr_lang = lang_map[lang]
                    break
            
            self.ocr_engine = PaddleOCR(
                use_angle_cls=True,
                lang=ocr_lang,
                use_gpu=use_gpu,
                show_log=False
            )
            
            print(f"PaddleOCR初始化成功 ({'GPU' if use_gpu else 'CPU'}) - 语言: {ocr_lang}")
            
        except Exception as e:
            print(f"PaddleOCR初始化失败: {e}")
            raise
    
    def _init_easyocr(self):
        """初始化EasyOCR"""
        if not OCR_ENGINES.get('easyocr', False):
            raise RuntimeError("EasyOCR未安装")
        
        try:
            gpu_available = self._check_gpu_available()
            use_gpu = self.use_gpu and gpu_available
            
            if self.use_gpu and not gpu_available:
                print("警告：请求使用GPU但未检测到CUDA支持，将使用CPU")
            
            self.ocr_engine = easyocr.Reader(self.languages, gpu=use_gpu)
            print(f"EasyOCR初始化成功 ({'GPU' if use_gpu else 'CPU'})")
            
        except Exception as e:
            print(f"EasyOCR初始化失败: {e}")
            raise
    
    def _init_tesseract(self):
        """初始化Tesseract"""
        if not OCR_ENGINES.get('tesseract', False):
            raise RuntimeError("Tesseract未安装")
        
        try:
            # 检查tesseract是否可用
            result = subprocess.run(['tesseract', '--version'], 
                                  capture_output=True, text=True)
            if result.returncode != 0:
                raise RuntimeError("Tesseract不可用")
            
            print("Tesseract初始化成功")
            
        except FileNotFoundError:
            raise RuntimeError("Tesseract未安装或不在PATH中")
    
    def _init_trocr(self):
        """初始化TrOCR"""
        if not OCR_ENGINES.get('trocr', False):
            raise RuntimeError("TrOCR未安装")
        
        try:
            # 加载中文TrOCR模型
            model_name = "microsoft/trocr-base-printed"
            self.processor = TrOCRProcessor.from_pretrained(model_name)
            self.model = VisionEncoderDecoderModel.from_pretrained(model_name)
            
            if self.use_gpu and self._check_gpu_available():
                self.model = self.model.cuda()
                print("TrOCR初始化成功 (GPU)")
            else:
                print("TrOCR初始化成功 (CPU)")
                
        except Exception as e:
            print(f"TrOCR初始化失败: {e}")
            raise
    
    def _check_gpu_available(self):
        """检测GPU是否可用"""
        try:
            import torch
            return torch.cuda.is_available()
        except ImportError:
            try:
                import subprocess
                result = subprocess.run(['nvidia-smi'], 
                                      capture_output=True, text=True)
                return result.returncode == 0
            except (FileNotFoundError, subprocess.SubprocessError):
                return False
    
    def extract_subtitles_from_video(self, 
                                   video_path: str, 
                                   output_path: Optional[str] = None,
                                   sample_interval: float = 1.0,
                                   subtitle_region: Optional[Tuple[int, int, int, int]] = None,
                                   confidence_threshold: float = 0.5,
                                   progress_callback: Optional[callable] = None) -> List[Dict]:
        """
        从视频中提取硬字幕
        
        Args:
            video_path: 视频文件路径
            output_path: 输出字幕文件路径（可选）
            sample_interval: 采样间隔（秒）
            subtitle_region: 字幕区域坐标 (x1, y1, x2, y2)
            confidence_threshold: 置信度阈值
            progress_callback: 进度回调函数
            
        Returns:
            字幕数据列表，每个元素包含时间戳和文本
        """
        if not os.path.exists(video_path):
            raise FileNotFoundError(f"视频文件不存在: {video_path}")
            
        print(f"开始从视频中提取硬字幕: {Path(video_path).name}")
        print(f"使用OCR引擎: {self.engine_name}")
        
        # 打开视频文件
        cap = cv2.VideoCapture(video_path)
        if not cap.isOpened():
            raise RuntimeError(f"无法打开视频文件: {video_path}")
            
        # 获取视频信息
        fps = cap.get(cv2.CAP_PROP_FPS)
        total_frames = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
        duration = total_frames / fps if fps > 0 else 0
        
        print(f"视频信息: {duration:.2f}秒, {fps:.2f}fps, {total_frames}帧")
        
        # 计算采样帧间隔
        sample_frame_interval = max(1, int(fps * sample_interval))
        
        # 字幕区域检测
        if subtitle_region is None:
            subtitle_region = self._detect_subtitle_region(cap)
            
        if not subtitle_region:
            print("未检测到字幕区域，使用默认底部区域")
            width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
            height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
            subtitle_region = (0, int(height * 0.75), width, height)
            
        print(f"字幕区域: {subtitle_region}")
        
        # 提取字幕
        subtitles = []
        frame_count = 0
        ocr_count = 0
        last_text = ""
        last_time = 0
        
        try:
            while True:
                ret, frame = cap.read()
                if not ret:
                    break
                    
                # 按间隔采样
                if frame_count % sample_frame_interval == 0:
                    timestamp = frame_count / fps
                    ocr_count += 1
                    
                    try:
                        # 提取字幕区域
                        x1, y1, x2, y2 = subtitle_region
                        region_img = frame[y1:y2, x1:x2]
                        
                        # 预处理图像
                        processed_img = self._preprocess_image(region_img)
                        
                        # OCR识别
                        text = self._ocr_recognize(processed_img, confidence_threshold)
                        
                        # 过滤和清理文本
                        cleaned_text = self._clean_text(text)
                        
                        if cleaned_text and cleaned_text != last_text:
                            # 如果有前一个字幕，设置结束时间
                            if subtitles and last_text:
                                subtitles[-1]['end_time'] = timestamp
                            
                            subtitles.append({
                                'start_time': timestamp,
                                'end_time': timestamp + sample_interval,
                                'text': cleaned_text,
                                'confidence': 1.0,  # 置信度可以根据具体引擎调整
                                'region': subtitle_region
                            })
                            
                            print(f"[{timestamp:.1f}s] 识别到字幕: {cleaned_text}")
                            last_text = cleaned_text
                            last_time = timestamp
                        
                    except Exception as e:
                        print(f"处理帧 {frame_count} 时出错: {e}")
                        
                frame_count += 1
                
                # 进度回调
                if progress_callback and frame_count % (sample_frame_interval * 10) == 0:
                    progress = (frame_count / total_frames) * 100
                    progress_callback(f"OCR识别进度: {progress:.1f}% (帧数: {frame_count}/{total_frames}, OCR次数: {ocr_count}, 识别字幕数: {len(subtitles)})")
                
                # 定期清理内存
                if frame_count % (sample_frame_interval * 50) == 0:
                    gc.collect()
                    
        finally:
            cap.release()
            
        print(f"OCR识别完成: 总帧数 {frame_count}, OCR次数 {ocr_count}, 识别字幕数 {len(subtitles)}")
        
        # 后处理：合并相邻的相同字幕
        if subtitles:
            subtitles = self._merge_adjacent_subtitles(subtitles)
            print(f"合并后字幕数: {len(subtitles)}")
        
        # 保存字幕文件
        if output_path and subtitles:
            self._save_srt(subtitles, output_path)
            print(f"字幕已保存到: {output_path}")
        
        # 最终进度更新
        if progress_callback:
            progress_callback(f"OCR识别完成: 共识别到 {len(subtitles)} 条字幕")
        
        return subtitles
    
    def _detect_subtitle_region(self, cap) -> Optional[Tuple[int, int, int, int]]:
        """检测字幕区域"""
        # 简单的字幕区域检测 - 通常在视频底部
        width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
        height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
        
        # 默认使用底部25%的区域
        return (0, int(height * 0.75), width, height)
    
    def _preprocess_image(self, image: np.ndarray) -> np.ndarray:
        """预处理图像以提高OCR识别效果"""
        # 转换为灰度图
        if len(image.shape) == 3:
            gray = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY)
        else:
            gray = image.copy()
        
        # 增强对比度
        clahe = cv2.createCLAHE(clipLimit=2.0, tileGridSize=(8,8))
        enhanced = clahe.apply(gray)
        
        # 二值化
        _, binary = cv2.threshold(enhanced, 0, 255, cv2.THRESH_BINARY + cv2.THRESH_OTSU)
        
        # 去噪
        kernel = np.ones((2,2), np.uint8)
        denoised = cv2.morphologyEx(binary, cv2.MORPH_CLOSE, kernel)
        
        # 放大图像以提高识别精度
        scale_factor = 2
        height, width = denoised.shape
        enlarged = cv2.resize(denoised, (width * scale_factor, height * scale_factor), 
                             interpolation=cv2.INTER_CUBIC)
        
        return enlarged
    
    def _ocr_recognize(self, image: np.ndarray, confidence_threshold: float) -> str:
        """OCR识别"""
        try:
            if self.engine_name == 'paddleocr':
                return self._paddleocr_recognize(image, confidence_threshold)
            elif self.engine_name == 'easyocr':
                return self._easyocr_recognize(image, confidence_threshold)
            elif self.engine_name == 'tesseract':
                return self._tesseract_recognize(image)
            elif self.engine_name == 'trocr':
                return self._trocr_recognize(image)
            else:
                return ""
        except Exception as e:
            print(f"OCR识别失败: {e}")
            return ""
    
    def _paddleocr_recognize(self, image: np.ndarray, confidence_threshold: float) -> str:
        """PaddleOCR识别"""
        try:
            results = self.ocr_engine.ocr(image, cls=True)
            
            if not results or not results[0]:
                return ""
            
            texts = []
            for line in results[0]:
                if line and len(line) >= 2:
                    text_info = line[1]
                    if isinstance(text_info, (list, tuple)) and len(text_info) >= 2:
                        text, confidence = text_info[0], text_info[1]
                        if confidence >= confidence_threshold:
                            texts.append(text)
            
            return ' '.join(texts)
            
        except Exception as e:
            print(f"PaddleOCR识别错误: {e}")
            return ""
    
    def _easyocr_recognize(self, image: np.ndarray, confidence_threshold: float) -> str:
        """EasyOCR识别"""
        try:
            results = self.ocr_engine.readtext(image)
            
            texts = []
            for result in results:
                if len(result) >= 3:
                    text, confidence = result[1], result[2]
                    if confidence >= confidence_threshold:
                        texts.append(text)
            
            return ' '.join(texts)
            
        except Exception as e:
            print(f"EasyOCR识别错误: {e}")
            return ""
    
    def _tesseract_recognize(self, image: np.ndarray) -> str:
        """Tesseract识别"""
        try:
            # 转换为PIL图像
            pil_image = Image.fromarray(image)
            
            # 设置语言
            lang = 'chi_sim+eng'  # 中文简体+英文
            if 'ja' in self.languages:
                lang += '+jpn'
            if 'ko' in self.languages:
                lang += '+kor'
            
            # OCR识别
            text = pytesseract.image_to_string(pil_image, lang=lang)
            return text.strip()
            
        except Exception as e:
            print(f"Tesseract识别错误: {e}")
            return ""
    
    def _trocr_recognize(self, image: np.ndarray) -> str:
        """TrOCR识别"""
        try:
            # 转换为PIL图像
            pil_image = Image.fromarray(image).convert('RGB')
            
            # 预处理
            pixel_values = self.processor(pil_image, return_tensors="pt").pixel_values
            
            if self.use_gpu and self._check_gpu_available():
                pixel_values = pixel_values.cuda()
            
            # 生成文本
            generated_ids = self.model.generate(pixel_values)
            generated_text = self.processor.batch_decode(generated_ids, skip_special_tokens=True)[0]
            
            return generated_text.strip()
            
        except Exception as e:
            print(f"TrOCR识别错误: {e}")
            return ""
    
    def _clean_text(self, text: str) -> str:
        """清理和过滤文本"""
        if not text:
            return ""
        
        # 去除多余空格
        text = ' '.join(text.split())
        
        # 过滤过短的文本
        if len(text) < 2:
            return ""
        
        # 过滤纯数字或纯符号
        if text.isdigit() or not any(c.isalnum() for c in text):
            return ""
        
        # 过滤常见的误识别内容
        noise_patterns = [
            r'^[^\w\s]*$',  # 纯符号
            r'^\d+$',       # 纯数字
            r'^[a-zA-Z]$',  # 单个字母
        ]
        
        for pattern in noise_patterns:
            if re.match(pattern, text):
                return ""
        
        return text
    
    def _merge_adjacent_subtitles(self, subtitles: List[Dict]) -> List[Dict]:
        """合并相邻的相同字幕"""
        if not subtitles:
            return []
        
        merged = []
        current = subtitles[0].copy()
        
        for i in range(1, len(subtitles)):
            next_sub = subtitles[i]
            
            # 如果文本相同且时间相近，合并
            if (current['text'] == next_sub['text'] and 
                next_sub['start_time'] - current['end_time'] < 2.0):
                current['end_time'] = next_sub['end_time']
            else:
                merged.append(current)
                current = next_sub.copy()
        
        merged.append(current)
        return merged
    
    def _save_srt(self, subtitles: List[Dict], output_path: str):
        """保存SRT格式字幕文件"""
        with open(output_path, 'w', encoding='utf-8') as f:
            for i, subtitle in enumerate(subtitles, 1):
                start_time = self._format_timestamp(subtitle['start_time'])
                end_time = self._format_timestamp(subtitle['end_time'])
                
                f.write(f"{i}\n")
                f.write(f"{start_time} --> {end_time}\n")
                f.write(f"{subtitle['text']}\n\n")
    
    def _format_timestamp(self, seconds: float) -> str:
        """格式化时间戳为SRT格式"""
        hours = int(seconds // 3600)
        minutes = int((seconds % 3600) // 60)
        secs = int(seconds % 60)
        millisecs = int((seconds % 1) * 1000)
        
        return f"{hours:02d}:{minutes:02d}:{secs:02d},{millisecs:03d}"


def main():
    """测试函数"""
    import argparse
    
    parser = argparse.ArgumentParser(description='高级OCR字幕提取器')
    parser.add_argument('video_path', help='视频文件路径')
    parser.add_argument('--output', '-o', help='输出字幕文件路径')
    parser.add_argument('--engine', choices=['auto', 'paddleocr', 'easyocr', 'tesseract', 'trocr'], 
                       default='auto', help='OCR引擎')
    parser.add_argument('--interval', type=float, default=1.0, help='采样间隔（秒）')
    parser.add_argument('--confidence', type=float, default=0.5, help='置信度阈值')
    parser.add_argument('--no-gpu', action='store_true', help='不使用GPU')
    
    args = parser.parse_args()
    
    # 创建提取器
    extractor = AdvancedOCRExtractor(
        languages=['ch', 'en'],
        use_gpu=not args.no_gpu,
        engine=args.engine
    )
    
    # 提取字幕
    subtitles = extractor.extract_subtitles_from_video(
        args.video_path,
        args.output,
        sample_interval=args.interval,
        confidence_threshold=args.confidence
    )
    
    print(f"提取完成，共 {len(subtitles)} 条字幕")


if __name__ == '__main__':
    main() 