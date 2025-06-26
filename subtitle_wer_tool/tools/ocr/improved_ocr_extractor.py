#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
改进的OCR字幕提取模块
集成PaddleOCR等更好的OCR引擎，提供更优的字幕识别效果
"""

import os
import cv2
import numpy as np
from pathlib import Path
from typing import List, Tuple, Optional, Dict
import re
import gc

# 检查可用的OCR引擎
OCR_ENGINES = {}

# PaddleOCR - 推荐，中文效果最好
try:
    from paddleocr import PaddleOCR
    # 额外检查paddle是否可用
    import paddle
    OCR_ENGINES['paddleocr'] = True
except ImportError as e:
    OCR_ENGINES['paddleocr'] = False
    print(f"PaddleOCR不可用: {e}")

# EasyOCR - 备选（暂时禁用以避免PyTorch依赖问题）
try:
    # 暂时禁用EasyOCR以避免PyTorch DLL问题
    # import easyocr
    OCR_ENGINES['easyocr'] = False
    print("EasyOCR已禁用（避免PyTorch依赖问题）")
except ImportError:
    OCR_ENGINES['easyocr'] = False

# Tesseract - 基础引擎
try:
    import pytesseract
    from PIL import Image
    OCR_ENGINES['tesseract'] = True
except ImportError:
    OCR_ENGINES['tesseract'] = False


class ImprovedOCRExtractor:
    """改进的OCR字幕提取器"""
    
    def __init__(self, 
                 languages: List[str] = None, 
                 use_gpu: bool = True,
                 engine: str = 'auto',
                 ocr_params: Dict = None):
        """
        初始化OCR提取器
        
        Args:
            languages: 识别语言列表
            use_gpu: 是否使用GPU加速
            engine: OCR引擎选择 ('auto', 'paddleocr', 'easyocr', 'tesseract')
            ocr_params: OCR参数字典，覆盖默认参数
        """
        self.languages = languages or ['ch', 'en']
        self.use_gpu = use_gpu
        self.engine_name = engine
        self.ocr_engine = None
        self.ocr_params = ocr_params or {}
        
        # 打印可用的OCR引擎
        print(f"可用的OCR引擎: {OCR_ENGINES}")
        
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
        
        # 检查指定的引擎是否可用
        if not OCR_ENGINES.get(self.engine_name, False):
            print(f"警告: 指定的OCR引擎 '{self.engine_name}' 不可用，尝试自动选择...")
            # 自动回退到可用的引擎
            if OCR_ENGINES.get('easyocr', False):
                self.engine_name = 'easyocr'
                print("自动选择: EasyOCR")
            elif OCR_ENGINES.get('tesseract', False):
                self.engine_name = 'tesseract'
                print("自动选择: Tesseract")
            elif OCR_ENGINES.get('paddleocr', False):
                self.engine_name = 'paddleocr'
                print("自动选择: PaddleOCR")
            else:
                raise RuntimeError("未找到任何可用的OCR引擎")
        
        print(f"初始化OCR引擎: {self.engine_name}")
        
        if self.engine_name == 'paddleocr':
            self._initialize_paddleocr()
        elif self.engine_name == 'easyocr':
            self._init_easyocr()
        elif self.engine_name == 'tesseract':
            self._init_tesseract()
        else:
            raise ValueError(f"不支持的OCR引擎: {self.engine_name}")
    
    def _get_default_paddleocr_params(self) -> Dict:
        """获取PaddleOCR 3.0.0稳定版的默认参数"""
        default_params = {
            # 核心设置
            'lang': self.languages[0] if self.languages else 'ch',
            'ocr_version': 'PP-OCRv5',  # 使用最新的PP-OCRv5模型
            
            # 模块开关
            'use_doc_orientation_classify': False,  # 文档方向分类
            'use_doc_unwarping': False,  # 文档展平
            'use_textline_orientation': True,  # 文本行方向分类
            
            # 文本检测参数
            'text_det_limit_side_len': 64,  # 图像边长限制
            'text_det_limit_type': 'min',  # 边长限制类型
            'text_det_thresh': 0.3,  # 像素阈值
            'text_det_box_thresh': 0.6,  # 框阈值
            'text_det_unclip_ratio': 2.0,  # 扩展系数
            
            # 文本识别参数
            'text_rec_score_thresh': 0.0,  # 识别分数阈值
            'text_recognition_batch_size': 1,  # 批次大小
            
            # 性能参数
            'enable_mkldnn': True,  # MKL-DNN加速
            'cpu_threads': 8,  # CPU线程数
            'enable_hpi': False,  # 高性能推理
            'use_tensorrt': False,  # TensorRT加速
            'precision': 'fp32',  # 计算精度
            'mkldnn_cache_capacity': 10,  # MKL-DNN缓存容量
        }
        
        # 用自定义参数覆盖默认参数
        default_params.update(self.ocr_params)
        
        return default_params

    def _initialize_paddleocr(self) -> bool:
        """初始化PaddleOCR 3.0.0稳定版"""
        try:
            from paddleocr import PaddleOCR
            
            # 检查CUDA是否可用
            try:
                import paddle
                cuda_available = paddle.device.is_compiled_with_cuda()
                gpu_count = paddle.device.cuda.device_count()
                print(f"CUDA可用: {cuda_available}, GPU数量: {gpu_count}")
            except:
                cuda_available = False
                gpu_count = 0
            
            # 获取完整的参数配置
            paddleocr_params = self._get_default_paddleocr_params()
            
            # 根据GPU可用性调整设备设置
            if self.use_gpu and cuda_available and gpu_count > 0:
                paddleocr_params['device'] = 'gpu'
                print("使用GPU加速的PaddleOCR 3.0.0")
            else:
                paddleocr_params['device'] = 'cpu'
                print("使用CPU版本的PaddleOCR 3.0.0")
                if self.use_gpu:
                    print("警告：请求使用GPU但未检测到CUDA支持，将使用CPU")
            
            # 过滤出PaddleOCR构造函数支持的参数
            valid_params = self._filter_valid_paddleocr_params(paddleocr_params)
            
            print(f"PaddleOCR初始化参数: {list(valid_params.keys())}")
            
            # 初始化PaddleOCR
            self.ocr_engine = PaddleOCR(**valid_params)
            
            print("✅ PaddleOCR 3.0.0初始化成功")
            return True
            
        except Exception as e:
            print(f"❌ PaddleOCR初始化失败: {e}")
            import traceback
            traceback.print_exc()
            return False
    
    def _filter_valid_paddleocr_params(self, params: Dict) -> Dict:
        """过滤出PaddleOCR构造函数支持的参数"""
        # PaddleOCR 3.0.0支持的参数列表
        valid_param_names = {
            # 基础参数
            'lang', 'device', 'ocr_version',
            
            # 模块开关参数
            'use_doc_orientation_classify', 'use_doc_unwarping', 'use_textline_orientation',
            
            # 模型名称参数
            'doc_orientation_classify_model_name', 'doc_unwarping_model_name',
            'text_detection_model_name', 'textline_orientation_model_name', 
            'text_recognition_model_name',
            
            # 模型路径参数
            'doc_orientation_classify_model_dir', 'doc_unwarping_model_dir',
            'text_detection_model_dir', 'textline_orientation_model_dir', 
            'text_recognition_model_dir',
            
            # 检测参数
            'text_det_limit_side_len', 'text_det_limit_type', 'text_det_thresh',
            'text_det_box_thresh', 'text_det_unclip_ratio',
            
            # 识别参数
            'text_rec_score_thresh', 'text_recognition_batch_size',
            'textline_orientation_batch_size',
            
            # 性能参数
            'enable_mkldnn', 'cpu_threads', 'enable_hpi', 'use_tensorrt',
            'precision', 'mkldnn_cache_capacity',
        }
        
        # 过滤有效参数
        filtered_params = {k: v for k, v in params.items() if k in valid_param_names}
        
        return filtered_params
    
    def _init_easyocr(self):
        """初始化EasyOCR"""
        if not OCR_ENGINES.get('easyocr', False):
            raise RuntimeError("EasyOCR未安装")
        
        try:
            gpu_available = self._check_gpu_available()
            use_gpu = self.use_gpu and gpu_available
            
            if self.use_gpu and not gpu_available:
                print("警告：请求使用GPU但未检测到CUDA支持，将使用CPU")
            
            # EasyOCR语言代码映射
            easyocr_lang_map = {
                'ch': 'ch_sim',
                'ch_sim': 'ch_sim',
                'en': 'en',
                'ja': 'ja',
                'ko': 'ko'
            }
            
            # 转换语言代码
            easyocr_languages = []
            for lang in self.languages:
                if lang in easyocr_lang_map:
                    easyocr_languages.append(easyocr_lang_map[lang])
                else:
                    easyocr_languages.append(lang)
            
            self.ocr_engine = easyocr.Reader(easyocr_languages, gpu=use_gpu)
            print(f"EasyOCR初始化成功 ({'GPU' if use_gpu else 'CPU'}) - 语言: {easyocr_languages}")
            
        except Exception as e:
            print(f"EasyOCR初始化失败: {e}")
            raise
    
    def _init_tesseract(self):
        """初始化Tesseract"""
        if not OCR_ENGINES.get('tesseract', False):
            raise RuntimeError("Tesseract未安装")
        
        try:
            # 检查tesseract是否可用
            import subprocess
            result = subprocess.run(['tesseract', '--version'], 
                                  capture_output=True, text=True)
            if result.returncode != 0:
                raise RuntimeError("Tesseract不可用")
            
            print("Tesseract初始化成功")
            
        except FileNotFoundError:
            raise RuntimeError("Tesseract未安装或不在PATH中")
    
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
                                   sample_interval: float = 2.0,
                                   subtitle_region: Optional[Tuple[int, int, int, int]] = None,
                                   confidence_threshold: float = 0.2,
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
        width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
        height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
        
        print(f"视频信息: {duration:.2f}秒, {fps:.2f}fps, {total_frames}帧")
        print(f"视频尺寸: {width}x{height}")
        
        # 计算采样帧间隔
        sample_frame_interval = max(1, int(fps * sample_interval))
        
        # 字幕区域检测 - 专注于屏幕下1/4区域
        if subtitle_region is None:
            # 设置为下1/4区域，稍微向上扩展一点以确保覆盖字幕
            subtitle_y_start = int(height * 0.7)  # 从70%高度开始
            subtitle_region = (0, subtitle_y_start, width, height)
            
        print(f"字幕识别区域: {subtitle_region} (区域高度: {subtitle_region[3] - subtitle_region[1]}像素)")
        
        # 提取字幕
        subtitles = []
        frame_count = 0
        ocr_count = 0
        last_text = ""
        
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
                        
                        # 检查区域是否有效
                        if region_img.size == 0:
                            continue
                        
                        # 预处理图像 - 提供多种选项
                        processed_img = self._preprocess_image(region_img, method='minimal')
                        
                        # 保存前几帧的处理结果用于调试
                        if ocr_count <= 5:
                            try:
                                # 保存原始字幕区域
                                cv2.imwrite(f"debug_original_{ocr_count}.png", region_img)
                                # 保存处理后的图像
                                cv2.imwrite(f"debug_processed_{ocr_count}.png", processed_img)
                                print(f"调试: 已保存原始图像 debug_original_{ocr_count}.png")
                                print(f"调试: 已保存处理后图像 debug_processed_{ocr_count}.png")
                            except:
                                pass
                        
                        # OCR识别
                        text = self._ocr_recognize(processed_img, confidence_threshold)
                        
                        # 显示OCR原始结果（仅前几次）
                        if ocr_count <= 10:
                            print(f"[{timestamp:.1f}s] OCR原始结果: '{text}'")
                            print(f"[{timestamp:.1f}s] 图像尺寸: {processed_img.shape}")
                        
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
                                'confidence': 1.0,
                                'region': subtitle_region
                            })
                            
                            print(f"[{timestamp:.1f}s] ✅ 识别到字幕: {cleaned_text}")
                            last_text = cleaned_text
                        elif ocr_count <= 10:
                            # 显示前几次的清理结果
                            print(f"[{timestamp:.1f}s] 清理后文本: '{cleaned_text}' (与上次相同或为空)")
                        
                    except Exception as e:
                        print(f"处理帧 {frame_count} 时出错: {e}")
                        
                frame_count += 1
                
                # 显示进度
                if frame_count % (sample_frame_interval * 10) == 0:
                    progress = (frame_count / total_frames) * 100
                    progress_msg = f"OCR识别进度: {progress:.1f}% (帧数: {frame_count}/{total_frames}, OCR次数: {ocr_count}, 识别字幕数: {len(subtitles)})"
                    print(progress_msg)
                    if progress_callback:
                        progress_callback(progress_msg)
                
                # 定期清理内存
                if frame_count % (sample_frame_interval * 50) == 0:
                    gc.collect()
                    
        finally:
            cap.release()
            
        completion_msg = f"OCR识别完成: 总帧数 {frame_count}, OCR次数 {ocr_count}, 识别字幕数 {len(subtitles)}"
        print(completion_msg)
        if progress_callback:
            progress_callback(completion_msg)
        
        # 后处理：合并相邻的相同字幕
        if subtitles:
            subtitles = self._merge_adjacent_subtitles(subtitles)
            merge_msg = f"合并后字幕数: {len(subtitles)}"
            print(merge_msg)
            if progress_callback:
                progress_callback(merge_msg)
        
        # 保存字幕文件
        if output_path and subtitles:
            self._save_srt(subtitles, output_path)
            save_msg = f"字幕已保存到: {output_path}"
            print(save_msg)
            if progress_callback:
                progress_callback(save_msg)
        
        return subtitles
    
    def _preprocess_image(self, image: np.ndarray, method: str = 'minimal') -> np.ndarray:
        """预处理图像 - 专门优化黑底白字"""
        if method == 'none':
            return image
        
        # 对于黑底白字，我们需要特别的处理
        gray = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY) if len(image.shape) == 3 else image
        
        if method == 'minimal':
            # 最简单的处理：只做基本的对比度增强
            # 对于黑底白字，通常已经有很好的对比度
            clahe = cv2.createCLAHE(clipLimit=2.0, tileGridSize=(8,8))
            enhanced = clahe.apply(gray)
            return enhanced
            
        elif method == 'simple':
            # 简单处理：适度的锐化和对比度增强
            # 先增强对比度
        clahe = cv2.createCLAHE(clipLimit=3.0, tileGridSize=(8,8))
        enhanced = clahe.apply(gray)
        
            # 轻微锐化
            kernel = np.array([[-1,-1,-1], [-1,9,-1], [-1,-1,-1]])
            sharpened = cv2.filter2D(enhanced, -1, kernel)
            
            return sharpened
            
        elif method == 'full':
            # 完整处理：包括去噪和形态学操作
            # 对于黑底白字，我们要保持字体的完整性
            
            # 1. 去噪
            denoised = cv2.bilateralFilter(gray, 9, 75, 75)
            
            # 2. 增强对比度
            clahe = cv2.createCLAHE(clipLimit=3.0, tileGridSize=(8,8))
            enhanced = clahe.apply(denoised)
            
            # 3. 二值化处理
            # 对于黑底白字，我们使用反向二值化
            mean_val = np.mean(enhanced)
            if mean_val < 127:  # 图像较暗，可能是黑底白字
                _, binary = cv2.threshold(enhanced, 0, 255, cv2.THRESH_BINARY + cv2.THRESH_OTSU)
            else:  # 图像较亮
                _, binary = cv2.threshold(enhanced, 0, 255, cv2.THRESH_BINARY_INV + cv2.THRESH_OTSU)
        
            # 4. 形态学操作：去除噪点但保持文字结构
            kernel = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (2, 2))
            cleaned = cv2.morphologyEx(binary, cv2.MORPH_OPEN, kernel)
            
            return cleaned
        
        return gray
    
    def _fill_hollow_characters(self, binary_image: np.ndarray) -> np.ndarray:
        """专门填充空心字符的函数"""
        # 查找轮廓
        contours, _ = cv2.findContours(binary_image, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
        
        # 创建填充后的图像
        filled = binary_image.copy()
        
        for contour in contours:
            # 计算轮廓面积
            area = cv2.contourArea(contour)
            
            # 只处理足够大的轮廓（过滤噪声）
            if area > 50:  # 调整这个阈值
                # 填充轮廓
                cv2.drawContours(filled, [contour], -1, 255, -1)
        
        return filled
    
    def _ocr_recognize(self, image, confidence_threshold=0.3):
        """使用OCR识别文本 - 优化版本"""
        try:
            if self.engine_name == 'paddleocr':
                # 对于黑底白字，PaddleOCR通常效果较好
                # 使用更低的置信度阈值来提高检测率
                results = self.ocr_engine.ocr(image)
            
                if results and results[0] is not None:
            texts = []
            for line in results[0]:
                if line and len(line) >= 2:
                    text_info = line[1]
                    if isinstance(text_info, (list, tuple)) and len(text_info) >= 2:
                        text, confidence = text_info[0], text_info[1]
                                # 使用传入的置信度阈值
                        if confidence >= confidence_threshold:
                            texts.append(text)
                                    print(f"    GPU OCR识别: '{text}' (置信度: {confidence:.3f})")
            
            return ' '.join(texts)
            
            elif self.engine_name == 'easyocr' and self.ocr_engine:
                # EasyOCR处理
            results = self.ocr_engine.readtext(image)
            texts = []
                for (bbox, text, confidence) in results:
                    if confidence >= confidence_threshold:
                        texts.append(text)
            return ' '.join(texts)
            
        except Exception as e:
            # 静默处理错误，避免大量重复错误信息
            pass
        
            return ""
    
    def _clean_text(self, text: str) -> str:
        """清理和过滤文本 - 针对字幕优化"""
        if not text:
            return ""
        
        # 去除多余空格和换行符
        text = ' '.join(text.split())
        
        # 过滤过短的文本（降低阈值）
        if len(text) < 1:
            return ""
        
        # 移除常见的OCR噪声字符
        noise_chars = ['|', '_', '-', '=', '+', '*', '#', '@', '$', '%', '^', '&']
        cleaned = text
        for char in noise_chars:
            if cleaned == char or cleaned == char * len(cleaned):
                return ""
        
        # 过滤纯符号（但保留中文字符）
        if not any(c.isalnum() or ord(c) > 127 for c in text):
            return ""
        
        # 过滤常见的误识别内容（更宽松的规则）
        noise_patterns = [
            r'^[^\w\s\u4e00-\u9fff]*$',  # 纯符号（不包含中文）
            r'^[•·▪▫○●◦‣⁃]+$',  # 项目符号
            r'^[_\-=+*#@$%^&]+$',  # 连续符号
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
    
    parser = argparse.ArgumentParser(description='改进的OCR字幕提取器')
    parser.add_argument('video_path', help='视频文件路径')
    parser.add_argument('--output', '-o', help='输出字幕文件路径')
    parser.add_argument('--engine', choices=['auto', 'paddleocr', 'easyocr', 'tesseract'], 
                       default='auto', help='OCR引擎')
    parser.add_argument('--interval', type=float, default=2.0, help='采样间隔（秒）')
    parser.add_argument('--confidence', type=float, default=0.2, help='置信度阈值')
    parser.add_argument('--no-gpu', action='store_true', help='不使用GPU')
    
    args = parser.parse_args()
    
    # 创建提取器
    extractor = ImprovedOCRExtractor(
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