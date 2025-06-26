#!/usr/bin/env python3
"""
修复死锁问题的PaddleOCR字幕提取器
主要修复：
1. 添加超时控制机制
2. 优化内存管理
3. 改进错误处理
4. 添加进度监控
"""

import os
import cv2
import numpy as np
from pathlib import Path
from typing import List, Dict, Optional, Tuple
import time
import threading
import gc
import argparse

# 检查PaddleOCR可用性
try:
    from paddleocr import PaddleOCR
    PADDLEOCR_AVAILABLE = True
    print("✅ PaddleOCR可用")
except ImportError as e:
    PADDLEOCR_AVAILABLE = False
    print(f"❌ PaddleOCR不可用: {e}")

class TimeoutError(Exception):
    """超时异常"""
    pass

class OCRDeadlockMonitor:
    """OCR死锁监控器"""
    
    def __init__(self, timeout_seconds=30):
        self.timeout = timeout_seconds
        self.start_time = None
        self.last_activity = None
        self.is_running = False
        self.monitor_thread = None
    
    def start_monitor(self):
        """开始监控"""
        self.start_time = time.time()
        self.last_activity = time.time()
        self.is_running = True
        
        self.monitor_thread = threading.Thread(target=self._monitor_loop, daemon=True)
        self.monitor_thread.start()
    
    def update_activity(self):
        """更新活动时间"""
        self.last_activity = time.time()
    
    def stop_monitor(self):
        """停止监控"""
        self.is_running = False
    
    def _monitor_loop(self):
        """监控循环"""
        while self.is_running:
            current_time = time.time()
            
            # 检查是否超时
            if self.last_activity and (current_time - self.last_activity) > self.timeout:
                print(f"⚠️ 检测到可能的死锁 (无活动时间: {current_time - self.last_activity:.1f}秒)")
                print("正在尝试恢复...")
                break
            
            time.sleep(1)  # 每秒检查一次

class FixedPaddleOCRExtractor:
    """修复死锁问题的PaddleOCR提取器"""
    
    def __init__(self, 
                 preset: str = 'subtitle',
                 custom_params: Dict = None,
                 use_gpu: bool = True,
                 timeout_seconds: int = 30):
        """
        初始化提取器
        """
        if not PADDLEOCR_AVAILABLE:
            raise RuntimeError("PaddleOCR未安装或版本不兼容")
            
        self.preset = preset
        self.custom_params = custom_params or {}
        self.use_gpu = use_gpu
        self.timeout_seconds = timeout_seconds
        self.ocr_engine = None
        self.monitor = OCRDeadlockMonitor(timeout_seconds)
        
        # 初始化OCR引擎
        self._initialize_paddleocr()
        
    def _initialize_paddleocr(self) -> bool:
        """初始化PaddleOCR，使用渐进式配置策略"""
        try:
            print("初始化PaddleOCR...")
            
            # 配置策略：从最保守到最优化
            configs_to_try = [
                # 最保守配置 - CPU only, 单线程
                {
                    'lang': 'ch',
                    'device': 'cpu',
                    'show_log': False,
                    'enable_mkldnn': False,
                    'cpu_threads': 1
                },
                
                # 中等配置 - 多线程CPU
                {
                    'lang': 'ch',
                    'device': 'cpu',
                    'show_log': False,
                    'enable_mkldnn': True,
                    'cpu_threads': min(4, os.cpu_count() or 4)
                },
                
                # GPU配置（如果用户要求）
                {
                    'lang': 'ch',
                    'device': 'gpu',
                    'show_log': False
                } if self.use_gpu else None
            ]
            
            # 移除None值
            configs_to_try = [c for c in configs_to_try if c is not None]
            
            # 逐个尝试配置
            for i, config in enumerate(configs_to_try):
                try:
                    print(f"尝试配置 {i+1}: {config}")
                    
                    # 使用超时控制初始化
                    start_time = time.time()
                    self.ocr_engine = PaddleOCR(**config)
                    init_time = time.time() - start_time
                    
                    print(f"✅ 配置 {i+1} 初始化成功 (耗时: {init_time:.2f}秒)")
                    
                    # 测试OCR引擎是否正常工作
                    if self._test_ocr_engine():
                        return True
                    else:
                        print(f"❌ 配置 {i+1} 功能测试失败")
                        self.ocr_engine = None
                        continue
                        
                except Exception as e:
                    print(f"❌ 配置 {i+1} 失败: {str(e)[:100]}...")
                    self.ocr_engine = None
                    continue
            
            print("❌ 所有配置都失败")
            return False
                
        except Exception as e:
            print(f"❌ 初始化失败: {e}")
            return False
    
    def _test_ocr_engine(self) -> bool:
        """测试OCR引擎是否正常工作"""
        try:
            # 创建简单测试图像
            test_image = np.ones((50, 100, 3), dtype=np.uint8) * 255
            cv2.putText(test_image, 'Test', (10, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 0, 0), 2)
            
            # 超时测试
            start_time = time.time()
            result = self.ocr_engine.ocr(test_image)
            test_time = time.time() - start_time
            
            if test_time > 10:  # 如果测试就超过10秒，说明有问题
                print(f"⚠️ OCR引擎响应过慢 (测试耗时: {test_time:.2f}秒)")
                return False
            
            print(f"✅ OCR引擎功能测试通过 (耗时: {test_time:.2f}秒)")
            return True
            
        except Exception as e:
            print(f"❌ OCR引擎测试失败: {e}")
            return False
    
    def extract_subtitles_from_video(self, 
                                   video_path: str, 
                                   output_path: Optional[str] = None,
                                   sample_interval: float = 2.0,
                                   subtitle_region: Optional[Tuple[int, int, int, int]] = None,
                                   confidence_threshold: float = 0.2,
                                   progress_callback: Optional[callable] = None,
                                   max_frames: Optional[int] = None) -> List[Dict]:
        """从视频中提取硬字幕 - 带死锁检测和恢复"""
        
        if not os.path.exists(video_path):
            raise FileNotFoundError(f"视频文件不存在: {video_path}")
        
        if self.ocr_engine is None:
            raise RuntimeError("OCR引擎未初始化")
            
        print(f"开始从视频中提取硬字幕: {Path(video_path).name}")
        print(f"使用预设配置: {self.preset}")
        print(f"死锁检测超时: {self.timeout_seconds}秒")
        
        # 启动死锁监控
        self.monitor.start_monitor()
        
        # 打开视频文件
        cap = cv2.VideoCapture(video_path)
        if not cap.isOpened():
            raise RuntimeError(f"无法打开视频文件: {video_path}")
            
        try:
            # 获取视频信息
            fps = cap.get(cv2.CAP_PROP_FPS)
            frame_count = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
            duration = frame_count / fps
            
            print(f"视频信息: {frame_count}帧, {fps:.2f}fps, {duration:.2f}秒")
            
            # 计算采样间隔
            sample_frame_interval = max(1, int(fps * sample_interval))
            
            # 限制处理帧数（用于测试或避免过长处理）
            if max_frames:
                total_samples = min(max_frames, frame_count // sample_frame_interval)
                print(f"限制处理帧数: {max_frames}")
            else:
                total_samples = frame_count // sample_frame_interval
            
            print(f"采样设置: 每{sample_interval}秒采样一次，共{total_samples}个样本")
            
            subtitles = []
            processed_frames = 0
            failed_ocr_count = 0
            start_time = time.time()
            
            # 处理循环
            for sample_idx in range(total_samples):
                frame_idx = sample_idx * sample_frame_interval
                
                # 更新监控活动
                self.monitor.update_activity()
                
                try:
                    # 设置帧位置
                    cap.set(cv2.CAP_PROP_POS_FRAMES, frame_idx)
                    ret, frame = cap.read()
                    
                    if not ret:
                        print(f"⚠️ 跳过帧 {frame_idx} (读取失败)")
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
                    
                    # OCR识别（带超时控制）
                    ocr_result = self._ocr_recognize_with_timeout(frame, confidence_threshold)
                    
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
                    if progress_callback and processed_frames % 5 == 0:
                        progress = processed_frames / total_samples * 100
                        elapsed = time.time() - start_time
                        estimated_total = elapsed / processed_frames * total_samples if processed_frames > 0 else 0
                        remaining = estimated_total - elapsed
                        
                        progress_msg = (f"处理进度: {progress:.1f}% "
                                      f"({processed_frames}/{total_samples}) "
                                      f"已用时: {elapsed:.1f}s "
                                      f"预计剩余: {remaining:.1f}s "
                                      f"识别字幕: {len(subtitles)}条")
                        progress_callback(progress_msg)
                    
                    # 定期清理内存
                    if processed_frames % 50 == 0:
                        gc.collect()
                    
                    # 错误恢复机制
                    if failed_ocr_count > 10:
                        print("⚠️ OCR失败次数过多，可能存在问题")
                        break
                    
                except Exception as e:
                    failed_ocr_count += 1
                    print(f"处理帧 {frame_idx} 时出错: {e}")
                    
                    if failed_ocr_count > 20:
                        print("❌ 错误次数过多，停止处理")
                        break
            
            # 停止监控
            self.monitor.stop_monitor()
            
            # 最终统计
            total_time = time.time() - start_time
            if processed_frames > 0:
                print(f"处理完成: {processed_frames}帧, 耗时: {total_time:.2f}秒, 平均: {total_time/processed_frames:.2f}秒/帧")
            
            # 后处理：合并相邻的重复字幕
            if subtitles:
                original_count = len(subtitles)
                subtitles = self._merge_adjacent_subtitles(subtitles)
                print(f"字幕合并: {original_count} -> {len(subtitles)}")
            
            # 保存字幕文件
            if output_path and subtitles:
                self._save_srt(subtitles, output_path)
                print(f"字幕已保存到: {output_path}")
            
            print(f"字幕提取完成，共提取到 {len(subtitles)} 条字幕")
            return subtitles
            
        finally:
            cap.release()
            self.monitor.stop_monitor()
    
    def _ocr_recognize_with_timeout(self, image, confidence_threshold=0.3, timeout_seconds=None):
        """带超时控制的OCR识别"""
        if timeout_seconds is None:
            timeout_seconds = self.timeout_seconds
        
        if self.ocr_engine is None:
            return None
        
        # 使用线程和超时控制
        result_container = [None]
        exception_container = [None]
        
        def ocr_worker():
            try:
                # 尝试多种API调用方式
                result = None
                
                try:
                    result = self.ocr_engine.ocr(image, cls=True)
                except TypeError:
                    try:
                        result = self.ocr_engine.ocr(image)
                    except Exception:
                        result = self.ocr_engine(image)
                
                # 处理结果
                if result and result[0]:
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
        worker_thread = threading.Thread(target=ocr_worker, daemon=True)
        start_time = time.time()
        worker_thread.start()
        
        # 等待结果或超时
        worker_thread.join(timeout_seconds)
        elapsed = time.time() - start_time
        
        if worker_thread.is_alive():
            print(f"⚠️ OCR识别超时 ({elapsed:.2f}秒)")
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
            if (self._texts_similar(current['text'], next_subtitle['text']) and 
                next_subtitle['timestamp'] - current['timestamp'] <= 5.0):
                current['end_timestamp'] = next_subtitle['timestamp']
            else:
                if 'end_timestamp' not in current:
                    current['end_timestamp'] = current['timestamp'] + 2.0
                merged.append(current)
                current = next_subtitle.copy()
        
        if 'end_timestamp' not in current:
            current['end_timestamp'] = current['timestamp'] + 2.0
        merged.append(current)
        
        return merged
    
    def _texts_similar(self, text1: str, text2: str, threshold: float = 0.8) -> bool:
        """检查两个文本是否相似"""
        if not text1 or not text2:
            return False
        
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

def main():
    """主函数"""
    parser = argparse.ArgumentParser(description='修复版PaddleOCR字幕提取器')
    parser.add_argument('video_path', help='输入视频文件路径')
    parser.add_argument('-o', '--output', help='输出字幕文件路径')
    parser.add_argument('-i', '--interval', type=float, default=2.0, help='采样间隔(秒)')
    parser.add_argument('-t', '--threshold', type=float, default=0.2, help='置信度阈值')
    parser.add_argument('--timeout', type=int, default=15, help='OCR超时时间(秒)')
    parser.add_argument('--max-frames', type=int, help='最大处理帧数(用于测试)')
    parser.add_argument('--cpu', action='store_true', help='强制使用CPU')
    
    args = parser.parse_args()
    
    try:
        extractor = FixedPaddleOCRExtractor(
            preset='subtitle',
            use_gpu=not args.cpu,
            timeout_seconds=args.timeout
        )
        
        if not args.output:
            video_path = Path(args.video_path)
            args.output = video_path.with_suffix('.srt')
        
        def progress_callback(message):
            print(f"\r{message}", end='', flush=True)
        
        subtitles = extractor.extract_subtitles_from_video(
            video_path=args.video_path,
            output_path=args.output,
            sample_interval=args.interval,
            confidence_threshold=args.threshold,
            progress_callback=progress_callback,
            max_frames=args.max_frames
        )
        
        print(f"\n字幕已保存到: {args.output}")
        
    except Exception as e:
        print(f"处理失败: {e}")
        import traceback
        traceback.print_exc()

if __name__ == "__main__":
    main() 