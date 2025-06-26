
#!/usr/bin/env python3
"""
改进的PaddleOCR字幕提取器 - 完全兼容版本
修复死锁问题：添加超时控制和异常处理
"""

import os
import cv2
import numpy as np
from pathlib import Path
from typing import List, Dict, Optional, Tuple
import argparse
import time
import threading
import gc

# 检查PaddleOCR可用性
try:
    from paddleocr import PaddleOCR
    PADDLEOCR_AVAILABLE = True
    print("✅ PaddleOCR可用")
except ImportError as e:
    PADDLEOCR_AVAILABLE = False
    print(f"❌ PaddleOCR不可用: {e}")

# 简化版配置管理器
class SimplePaddleOCRConfig:
    """简化的配置管理器，确保最大兼容性"""
    
    def get_config_from_preset(self, preset):
        """返回预设配置"""
        return {'lang': 'ch'}  # 最基本配置
    
    def merge_configs(self, base, override):
        """合并配置"""
        result = base.copy()
        result.update(override)
        return result

class PaddleOCRExtractor:
    """基于PaddleOCR的参数化字幕提取器 - 兼容版本"""
    
    def __init__(self, 
                 preset: str = 'subtitle',
                 custom_params: Dict = None,
                 use_gpu: bool = True):
        """
        初始化PaddleOCR提取器
        
        Args:
            preset: 预设配置名称
            custom_params: 自定义参数字典
            use_gpu: 是否优先使用GPU加速
        """
        if not PADDLEOCR_AVAILABLE:
            raise RuntimeError("PaddleOCR未安装或版本不兼容")
            
        self.preset = preset
        self.custom_params = custom_params or {}
        self.use_gpu = use_gpu
        self.config_manager = SimplePaddleOCRConfig()
        self.ocr_engine = None
        
        # 初始化OCR引擎
        self._initialize_paddleocr()
        
    def _initialize_paddleocr(self) -> bool:
        """初始化PaddleOCR，使用最保守的策略确保兼容性"""
        try:
            print("初始化PaddleOCR...")
            
            # 使用最基本的配置，逐步增加参数
            configs_to_try = [
                # 配置1：仅语言（最安全）
                {'lang': 'ch'},
                
                # 配置2：添加设备
                {'lang': 'ch', 'device': 'cpu'},
                
                # 配置3：如果用户要求GPU
                {'lang': 'ch', 'device': 'gpu'} if self.use_gpu else None
            ]
            
            # 移除None值
            configs_to_try = [c for c in configs_to_try if c is not None]
            
            # 逐个尝试配置
            for i, config in enumerate(configs_to_try):
                try:
                    print(f"尝试配置 {i+1}: {config}")
                    self.ocr_engine = PaddleOCR(**config)
                    print(f"✅ 配置 {i+1} 初始化成功")
                    return True
                except Exception as e:
                    print(f"❌ 配置 {i+1} 失败: {str(e)[:50]}...")
                    continue
            
            print("❌ 所有配置都失败")
            return False
                
        except Exception as e:
            print(f"❌ 初始化失败: {e}")
            return False
    
    def _adjust_device_config(self, config: Dict) -> Dict:
        """调整设备配置"""
        # 检查GPU可用性（简化版）
        if config.get('device') == 'gpu':
            try:
                import paddle
                if not paddle.device.is_compiled_with_cuda():
                    print("GPU不可用，使用CPU")
                    config['device'] = 'cpu'
            except:
                print("无法检测GPU，使用CPU")
                config['device'] = 'cpu'
        
        return config
    
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
        print(f"使用预设配置: {self.preset}")
        print(f"使用兼容版PaddleOCR")
        
        # 打开视频文件
        cap = cv2.VideoCapture(video_path)
        if not cap.isOpened():
            raise RuntimeError(f"无法打开视频文件: {video_path}")
            
        # 获取视频信息
        fps = cap.get(cv2.CAP_PROP_FPS)
        frame_count = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
        duration = frame_count / fps
        
        print(f"视频信息: {frame_count}帧, {fps:.2f}fps, {duration:.2f}秒")
        
        # 计算采样间隔
        sample_frame_interval = int(fps * sample_interval)
        total_samples = int(frame_count / sample_frame_interval)
        
        print(f"采样设置: 每{sample_interval}秒采样一次，共{total_samples}个样本")
        
        subtitles = []
        processed_frames = 0
        
        try:
            start_time = time.time()
            failed_count = 0
            
            for frame_idx in range(0, frame_count, sample_frame_interval):
                # 设置帧位置
                cap.set(cv2.CAP_PROP_POS_FRAMES, frame_idx)
                ret, frame = cap.read()
                
                if not ret:
                    print(f"⚠️ 无法读取帧 {frame_idx}")
                    failed_count += 1
                    if failed_count > 10:  # 连续失败太多次就停止
                        print("❌ 视频读取失败次数过多，停止处理")
                        break
                    continue
                
                # 计算时间戳
                timestamp = frame_idx / fps
                
                # 裁剪字幕区域
                if subtitle_region:
                    x1, y1, x2, y2 = subtitle_region
                    if x2 > frame.shape[1] or y2 > frame.shape[0]:
                        print(f"⚠️ 字幕区域超出帧范围，使用全帧")
                    else:
                        frame = frame[y1:y2, x1:x2]
                
                # OCR识别
                ocr_result = self._ocr_recognize(frame, confidence_threshold)
                
                if ocr_result:
                    subtitle_entry = {
                        'timestamp': timestamp,
                        'text': ocr_result,
                        'frame_idx': frame_idx
                    }
                    subtitles.append(subtitle_entry)
                    print(f"[{timestamp:.2f}s] {ocr_result}")
                
                processed_frames += 1
                
                # 进度回调
                if progress_callback and processed_frames % 5 == 0:  # 每5帧报告一次
                    progress = processed_frames / total_samples * 100
                    elapsed = time.time() - start_time
                    avg_time = elapsed / processed_frames
                    remaining_time = avg_time * (total_samples - processed_frames)
                    
                    progress_callback(f"处理进度: {progress:.1f}% ({processed_frames}/{total_samples}) "
                                    f"已用时: {elapsed:.1f}s 预计剩余: {remaining_time:.1f}s "
                                    f"识别字幕: {len(subtitles)}条")
                
                # 定期清理内存
                if processed_frames % 50 == 0:
                    gc.collect()
                    
        finally:
            cap.release()
        
        # 后处理：合并相邻的重复字幕
        subtitles = self._merge_adjacent_subtitles(subtitles)
        
        # 保存字幕文件
        if output_path:
            self._save_srt(subtitles, output_path)
        
        print(f"字幕提取完成，共提取到 {len(subtitles)} 条字幕")
        
        return subtitles
    
    def _ocr_recognize(self, image, confidence_threshold=0.3):
        """使用PaddleOCR识别文本 - 兼容版本，带超时控制"""
        if self.ocr_engine is None:
            print("OCR引擎未初始化")
            return None
            
        # 使用线程和超时控制避免死锁
        result_container = [None]
        exception_container = [None]
        
        def ocr_worker():
            try:
                # 尝试多种API调用方式以确保兼容性
                result = None
                
                # 方法1：尝试带cls参数
                try:
                    result = self.ocr_engine.ocr(image, cls=True)
                except TypeError:
                    # 方法2：不带cls参数
                    try:
                        result = self.ocr_engine.ocr(image)
                    except Exception:
                        # 方法3：最基本的调用方式
                        result = self.ocr_engine(image)
                
                if result and result[0]:
                    # 提取所有文本并按置信度过滤
                    texts = []
                    for line in result[0]:
                        if len(line) >= 2 and len(line[1]) >= 2:
                            text, confidence = line[1]
                            if confidence >= confidence_threshold:
                                texts.append(text)
                    
                    if texts:
                        result_container[0] = ' '.join(texts)
                
            except Exception as e:
                exception_container[0] = e
        
        # 启动OCR工作线程
        import threading
        worker_thread = threading.Thread(target=ocr_worker, daemon=True)
        start_time = time.time()
        worker_thread.start()
        
        # 等待结果或超时（15秒超时）
        timeout_seconds = 15
        worker_thread.join(timeout_seconds)
        elapsed = time.time() - start_time
        
        if worker_thread.is_alive():
            print(f"⚠️ OCR识别超时 ({elapsed:.2f}秒)，跳过此帧")
            return None
        
        if exception_container[0]:
            print(f"OCR识别错误: {exception_container[0]}")
            return None
        
        return result_container[0]
    
    def _merge_adjacent_subtitles(self, subtitles: List[Dict]) -> List[Dict]:
        """合并相邻的相似字幕"""
        if not subtitles:
            return []
        
        merged = []
        current = subtitles[0].copy()
        
        for next_subtitle in subtitles[1:]:
            # 如果文本相似且时间接近，则合并
            if (self._texts_similar(current['text'], next_subtitle['text']) and 
                next_subtitle['timestamp'] - current['timestamp'] <= 5.0):
                # 更新结束时间为下一个字幕的时间
                current['end_timestamp'] = next_subtitle['timestamp']
            else:
                # 添加当前字幕到结果中
                if 'end_timestamp' not in current:
                    current['end_timestamp'] = current['timestamp'] + 2.0
                merged.append(current)
                current = next_subtitle.copy()
        
        # 添加最后一个字幕
        if 'end_timestamp' not in current:
            current['end_timestamp'] = current['timestamp'] + 2.0
        merged.append(current)
        
        return merged
    
    def _texts_similar(self, text1: str, text2: str, threshold: float = 0.8) -> bool:
        """检查两个文本是否相似"""
        if not text1 or not text2:
            return False
        
        # 简单的相似度计算：相同字符数/总字符数
        set1 = set(text1)
        set2 = set(text2)
        
        intersection = len(set1.intersection(set2))
        union = len(set1.union(set2))
        
        if union == 0:
            return False
        
        similarity = intersection / union
        return similarity >= threshold
    
    def _save_srt(self, subtitles: List[Dict], output_path: str):
        """保存为SRT格式"""
        with open(output_path, 'w', encoding='utf-8') as f:
            for i, subtitle in enumerate(subtitles, 1):
                start_time = self._format_timestamp(subtitle['timestamp'])
                end_time = self._format_timestamp(subtitle.get('end_timestamp', subtitle['timestamp'] + 2.0))
                
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
    
    def get_config_info(self) -> Dict:
        """获取当前配置信息"""
        return {
            'preset': self.preset,
            'custom_params': self.custom_params,
            'use_gpu': self.use_gpu,
            'paddleocr_available': PADDLEOCR_AVAILABLE,
            'ocr_engine_ready': self.ocr_engine is not None
        }
    
    def print_config_info(self):
        """打印配置信息"""
        info = self.get_config_info()
        print("=== PaddleOCR字幕提取器配置信息 ===")
        print(f"预设配置: {info['preset']}")
        print(f"使用GPU: {info['use_gpu']}")
        print(f"PaddleOCR可用: {info['paddleocr_available']}")
        print(f"OCR引擎就绪: {info['ocr_engine_ready']}")
        print(f"自定义参数: {info['custom_params']}")


def main():
    """主函数 - 命令行接口"""
    parser = argparse.ArgumentParser(description='PaddleOCR字幕提取器')
    parser.add_argument('video_path', help='输入视频文件路径')
    parser.add_argument('-o', '--output', help='输出字幕文件路径')
    parser.add_argument('-p', '--preset', default='subtitle', help='预设配置')
    parser.add_argument('-i', '--interval', type=float, default=2.0, help='采样间隔(秒)')
    parser.add_argument('-t', '--threshold', type=float, default=0.2, help='置信度阈值')
    parser.add_argument('-r', '--region', type=int, nargs=4, metavar=('X1', 'Y1', 'X2', 'Y2'), help='字幕区域坐标')
    parser.add_argument('--cpu', action='store_true', help='强制使用CPU')
    parser.add_argument('--info', action='store_true', help='显示配置信息')
    
    args = parser.parse_args()
    
    try:
        # 创建提取器
        extractor = PaddleOCRExtractor(
            preset=args.preset,
            use_gpu=not args.cpu
        )
        
        if args.info:
            extractor.print_config_info()
            return
        
        # 设置输出路径
        if not args.output:
            video_path = Path(args.video_path)
            args.output = video_path.with_suffix('.srt')
        
        # 进度回调函数
        def progress_callback(message):
            print(f"\r{message}", end='', flush=True)
        
        # 提取字幕
        subtitles = extractor.extract_subtitles_from_video(
            video_path=args.video_path,
            output_path=args.output,
            sample_interval=args.interval,
            subtitle_region=tuple(args.region) if args.region else None,
            confidence_threshold=args.threshold,
            progress_callback=progress_callback
        )
        
        print(f"\n字幕已保存到: {args.output}")
        
    except Exception as e:
        print(f"处理失败: {e}")
        import traceback
        traceback.print_exc()


if __name__ == "__main__":
    main()
 