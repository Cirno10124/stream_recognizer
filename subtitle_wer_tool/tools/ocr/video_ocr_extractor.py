#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
视频OCR字幕提取模块
功能：从视频画面中识别硬字幕（渲染在画面上的字幕）
"""

import os
import cv2
import numpy as np
import tempfile
import subprocess
from pathlib import Path
from typing import List, Tuple, Optional, Dict
import json
import re
from datetime import datetime, timedelta

try:
    import easyocr
    EASYOCR_AVAILABLE = True
except ImportError:
    EASYOCR_AVAILABLE = False

try:
    import pytesseract
    from PIL import Image
    TESSERACT_AVAILABLE = True
except ImportError:
    TESSERACT_AVAILABLE = False


class VideoOCRExtractor:
    """视频OCR字幕提取器"""
    
    def __init__(self, languages: List[str] = None, use_gpu: bool = True):
        """
        初始化OCR提取器
        
        Args:
            languages: 识别语言列表，如 ['ch_sim', 'en']
            use_gpu: 是否使用GPU加速
        """
        self.languages = languages or ['ch_sim', 'en']
        self.use_gpu = use_gpu
        self.reader = None
        self.tesseract_available = TESSERACT_AVAILABLE
        self.easyocr_available = EASYOCR_AVAILABLE
        
        # 初始化OCR引擎
        self._init_ocr_engine()
        
    def _init_ocr_engine(self):
        """初始化OCR引擎"""
        if self.easyocr_available:
            try:
                # 检测GPU可用性
                gpu_available = self._check_gpu_available()
                use_gpu = self.use_gpu and gpu_available
                
                if self.use_gpu and not gpu_available:
                    print("警告：请求使用GPU但未检测到CUDA支持，将使用CPU")
                
                self.reader = easyocr.Reader(self.languages, gpu=use_gpu)
                print(f"使用EasyOCR引擎 ({'GPU' if use_gpu else 'CPU'})")
                
            except Exception as e:
                print(f"EasyOCR初始化失败: {e}")
                # 如果GPU初始化失败，尝试CPU
                if self.use_gpu:
                    try:
                        print("GPU初始化失败，尝试使用CPU...")
                        self.reader = easyocr.Reader(self.languages, gpu=False)
                        print("使用EasyOCR引擎 (CPU)")
                    except Exception as e2:
                        print(f"CPU初始化也失败: {e2}")
                        self.easyocr_available = False
                else:
                    self.easyocr_available = False
                
        if not self.easyocr_available and self.tesseract_available:
            try:
                # 检查tesseract是否可用
                result = subprocess.run(['tesseract', '--version'], 
                                      capture_output=True, text=True)
                if result.returncode == 0:
                    print("使用Tesseract引擎")
                else:
                    self.tesseract_available = False
            except FileNotFoundError:
                self.tesseract_available = False
                print("Tesseract未安装或不在PATH中")
                
        if not self.easyocr_available and not self.tesseract_available:
            raise RuntimeError("未找到可用的OCR引擎，请安装easyocr或pytesseract+Pillow")
    
    def _check_gpu_available(self):
        """检测GPU是否可用"""
        try:
            import torch
            return torch.cuda.is_available()
        except ImportError:
            try:
                # 如果没有torch，尝试检测CUDA
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
                                   confidence_threshold: float = 0.5) -> List[Dict]:
        """
        从视频中提取硬字幕
        
        Args:
            video_path: 视频文件路径
            output_path: 输出字幕文件路径（可选）
            sample_interval: 采样间隔（秒）
            subtitle_region: 字幕区域坐标 (x1, y1, x2, y2)
            confidence_threshold: 置信度阈值
            
        Returns:
            字幕数据列表，每个元素包含时间戳和文本
        """
        if not os.path.exists(video_path):
            raise FileNotFoundError(f"视频文件不存在: {video_path}")
            
        print(f"开始从视频中提取硬字幕: {Path(video_path).name}")
        
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
        sample_frame_interval = int(fps * sample_interval)
        
        # 如果没有指定字幕区域，尝试自动检测
        if subtitle_region is None:
            subtitle_region = self._detect_subtitle_regions(cap)
            
        if not subtitle_region:
            print("未检测到字幕区域，使用默认底部区域")
            width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
            height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
            subtitle_region = (0, int(height * 0.8), width, height)
            
        print(f"字幕区域: {subtitle_region}")
        
        # 提取字幕
        subtitles = []
        frame_count = 0
        ocr_count = 0
        
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
                        
                        # OCR识别
                        text = self._ocr_recognize(region_img, confidence_threshold)
                        
                        if text and text.strip():
                            subtitles.append({
                                'timestamp': timestamp,
                                'text': text.strip(),
                                'region': subtitle_region
                            })
                            print(f"[{timestamp:.1f}s] 识别到字幕: {text.strip()}")
                    except Exception as e:
                        print(f"处理帧 {frame_count} 时出错: {e}")
                            
                frame_count += 1
                
                # 显示进度 - 更频繁的进度更新
                if frame_count % (sample_frame_interval * 5) == 0:  # 每5个采样间隔显示一次
                    progress = (frame_count / total_frames) * 100
                    print(f"处理进度: {progress:.1f}% (已处理 {frame_count}/{total_frames} 帧, OCR次数: {ocr_count}, 识别到字幕: {len(subtitles)}条)")
                    
                # 定期清理内存
                if frame_count % (sample_frame_interval * 50) == 0:
                    import gc
                    gc.collect()
                    
        except Exception as e:
            print(f"视频处理过程中出现异常: {e}")
            import traceback
            traceback.print_exc()
        finally:
            cap.release()
            
        # 显示最终进度
        final_progress = (frame_count / total_frames) * 100
        print(f"视频处理完成: {final_progress:.1f}% (处理了 {frame_count}/{total_frames} 帧, OCR次数: {ocr_count})")
        
        # 后处理：合并相邻的相同文本
        subtitles = self._merge_adjacent_subtitles(subtitles)
        
        # 保存结果
        if output_path:
            self._save_srt(subtitles, output_path)
            
        print(f"字幕提取完成，共识别 {len(subtitles)} 条字幕")
        return subtitles
    
    def _detect_subtitle_regions(self, cap) -> Tuple[int, int, int, int]:
        """自动检测字幕区域"""
        print("正在检测字幕区域...")
        
        # 读取几个关键帧进行分析
        sample_frames = []
        total_frames = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
        
        for i in range(5):
            frame_pos = int(total_frames * (i + 1) / 6)
            cap.set(cv2.CAP_PROP_POS_FRAMES, frame_pos)
            ret, frame = cap.read()
            if ret:
                sample_frames.append(frame)
                
        if not sample_frames:
            return None
            
        # 分析帧差异，检测字幕区域
        regions = []
        height, width = sample_frames[0].shape[:2]
        
        # 检测底部区域（最常见的字幕位置）
        bottom_region = (0, int(height * 0.8), width, height)
        regions.append(bottom_region)
        
        # 检测顶部区域（某些视频的字幕在顶部）
        top_region = (0, 0, width, int(height * 0.2))
        regions.append(top_region)
        
        return regions[0] if regions else None
    
    def _ocr_recognize(self, image: np.ndarray, confidence_threshold: float) -> str:
        """OCR识别图像中的文字"""
        if self.easyocr_available and self.reader:
            try:
                # 检查图像是否有效
                if image is None or image.size == 0:
                    return ""
                
                # 预处理图像以提高识别率
                if len(image.shape) == 3:
                    gray = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY)
                else:
                    gray = image
                
                # 增强对比度
                gray = cv2.convertScaleAbs(gray, alpha=1.5, beta=0)
                
                results = self.reader.readtext(gray)
                texts = [text for (bbox, text, conf) in results if conf >= confidence_threshold]
                return ' '.join(texts)
            except Exception as e:
                print(f"EasyOCR识别错误: {e}")
                return ""
        elif self.tesseract_available:
            try:
                if image is None or image.size == 0:
                    return ""
                    
                pil_image = Image.fromarray(cv2.cvtColor(image, cv2.COLOR_BGR2RGB))
                text = pytesseract.image_to_string(pil_image, lang='chi_sim+eng')
                return text.strip()
            except Exception as e:
                print(f"Tesseract识别错误: {e}")
                return ""
        else:
            return ""
    
    def _merge_adjacent_subtitles(self, subtitles: List[Dict]) -> List[Dict]:
        """合并相邻的相同字幕"""
        if not subtitles:
            return subtitles
            
        merged = []
        current = subtitles[0].copy()
        
        for subtitle in subtitles[1:]:
            # 如果文本相同且时间间隔小于2秒，则合并
            if (subtitle['text'] == current['text'] and 
                subtitle['timestamp'] - current['timestamp'] <= 2.0):
                # 更新结束时间
                current['end_timestamp'] = subtitle['timestamp']
            else:
                # 添加结束时间
                if 'end_timestamp' not in current:
                    current['end_timestamp'] = current['timestamp']
                merged.append(current)
                current = subtitle.copy()
                
        # 处理最后一个
        if 'end_timestamp' not in current:
            current['end_timestamp'] = current['timestamp']
        merged.append(current)
        
        return merged
    
    def _save_srt(self, subtitles: List[Dict], output_path: str):
        """保存为SRT格式"""
        with open(output_path, 'w', encoding='utf-8') as f:
            for i, subtitle in enumerate(subtitles, 1):
                start_time = self._format_timestamp(subtitle['timestamp'])
                end_time = self._format_timestamp(subtitle.get('end_timestamp', subtitle['timestamp']))
                
                f.write(f"{i}\n")
                f.write(f"{start_time} --> {end_time}\n")
                f.write(f"{subtitle['text']}\n\n")
    
    def _format_timestamp(self, seconds: float) -> str:
        """格式化时间戳"""
        hours = int(seconds // 3600)
        minutes = int((seconds % 3600) // 60)
        secs = int(seconds % 60)
        millisecs = int((seconds % 1) * 1000)
        
        return f"{hours:02d}:{minutes:02d}:{secs:02d},{millisecs:03d}"


class VideoFrameExtractor:
    """视频帧提取器（用于调试和预览）"""
    
    @staticmethod
    def extract_frames(video_path: str, 
                      output_dir: str,
                      sample_interval: float = 5.0,
                      max_frames: int = 20) -> List[str]:
        """
        提取视频帧用于调试
        
        Args:
            video_path: 视频文件路径
            output_dir: 输出目录
            sample_interval: 采样间隔（秒）
            max_frames: 最大提取帧数
            
        Returns:
            提取的帧文件路径列表
        """
        if not os.path.exists(video_path):
            raise FileNotFoundError(f"视频文件不存在: {video_path}")
            
        os.makedirs(output_dir, exist_ok=True)
        
        cap = cv2.VideoCapture(video_path)
        if not cap.isOpened():
            raise RuntimeError(f"无法打开视频文件: {video_path}")
            
        fps = cap.get(cv2.CAP_PROP_FPS)
        sample_frame_interval = int(fps * sample_interval)
        
        frame_files = []
        frame_count = 0
        extracted_count = 0
        
        while extracted_count < max_frames:
            ret, frame = cap.read()
            if not ret:
                break
                
            if frame_count % sample_frame_interval == 0:
                timestamp = frame_count / fps
                frame_filename = f"frame_{timestamp:.2f}s.jpg"
                frame_path = os.path.join(output_dir, frame_filename)
                
                cv2.imwrite(frame_path, frame)
                frame_files.append(frame_path)
                extracted_count += 1
                
            frame_count += 1
            
        cap.release()
        
        print(f"提取了 {len(frame_files)} 个帧到 {output_dir}")
        return frame_files


def main():
    """测试函数"""
    import argparse
    
    parser = argparse.ArgumentParser(description='视频OCR字幕提取工具')
    parser.add_argument('video_path', help='视频文件路径')
    parser.add_argument('--output', '-o', help='输出SRT文件路径')
    parser.add_argument('--interval', '-i', type=float, default=1.0, help='采样间隔（秒）')
    parser.add_argument('--region', nargs=4, type=int, help='字幕区域 x1 y1 x2 y2')
    parser.add_argument('--confidence', '-c', type=float, default=0.5, help='置信度阈值')
    parser.add_argument('--languages', '-l', nargs='+', default=['ch_sim', 'en'], help='识别语言')
    parser.add_argument('--gpu', action='store_true', help='使用GPU加速')
    parser.add_argument('--no-gpu', action='store_true', help='强制使用CPU')
    parser.add_argument('--extract-frames', action='store_true', help='提取调试帧')
    
    args = parser.parse_args()
    
    # 确定GPU使用策略
    if args.no_gpu:
        use_gpu = False
    elif args.gpu:
        use_gpu = True
    else:
        # 默认尝试使用GPU
        use_gpu = True
    
    try:
        # 创建OCR提取器
        extractor = VideoOCRExtractor(languages=args.languages, use_gpu=use_gpu)
        
        # 提取字幕
        subtitles = extractor.extract_subtitles_from_video(
            args.video_path,
            args.output,
            args.interval,
            tuple(args.region) if args.region else None,
            args.confidence
        )
        
        # 显示结果
        print(f"\n提取的字幕:")
        for subtitle in subtitles:
            timestamp = extractor._format_timestamp(subtitle['timestamp'])
            print(f"[{timestamp}] {subtitle['text']}")
            
        # 提取调试帧
        if args.extract_frames:
            output_dir = "debug_frames"
            VideoFrameExtractor.extract_frames(args.video_path, output_dir)
            
    except Exception as e:
        print(f"错误: {e}")


if __name__ == '__main__':
    main() 