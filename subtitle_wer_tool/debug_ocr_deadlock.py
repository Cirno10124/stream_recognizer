#!/usr/bin/env python3
"""
调试OCR死锁问题的测试脚本
"""

import os
import cv2
import sys
import time
import threading
from pathlib import Path

def test_video_reading(video_path):
    """测试基础视频读取功能"""
    print(f"=== 测试视频读取功能 ===")
    print(f"视频路径: {video_path}")
    
    if not os.path.exists(video_path):
        print(f"❌ 视频文件不存在: {video_path}")
        return False
    
    try:
        # 测试基本视频打开
        cap = cv2.VideoCapture(video_path)
        if not cap.isOpened():
            print("❌ 无法打开视频文件")
            return False
        
        # 获取基本信息
        fps = cap.get(cv2.CAP_PROP_FPS)
        frame_count = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
        duration = frame_count / fps if fps > 0 else 0
        
        print(f"✅ 视频信息获取成功:")
        print(f"   - 帧数: {frame_count}")
        print(f"   - FPS: {fps:.2f}")
        print(f"   - 时长: {duration:.2f}秒")
        
        # 测试几个关键帧的读取
        test_frames = [0, frame_count//4, frame_count//2, frame_count*3//4]
        
        for test_frame in test_frames:
            if test_frame >= frame_count:
                continue
                
            print(f"测试读取帧 {test_frame}...")
            
            # 设置超时
            start_time = time.time()
            timeout = 10  # 10秒超时
            
            cap.set(cv2.CAP_PROP_POS_FRAMES, test_frame)
            ret, frame = cap.read()
            
            elapsed = time.time() - start_time
            
            if ret:
                print(f"   ✅ 帧 {test_frame} 读取成功 (耗时: {elapsed:.2f}秒)")
                print(f"   - 帧尺寸: {frame.shape}")
            else:
                print(f"   ❌ 帧 {test_frame} 读取失败 (耗时: {elapsed:.2f}秒)")
            
            if elapsed > timeout:
                print(f"   ⚠️ 帧读取超时 ({elapsed:.2f}秒)")
                cap.release()
                return False
        
        cap.release()
        print("✅ 视频读取测试完成")
        return True
        
    except Exception as e:
        print(f"❌ 视频读取测试异常: {e}")
        import traceback
        traceback.print_exc()
        return False

def test_paddleocr_basic():
    """测试PaddleOCR基础功能"""
    print(f"\n=== 测试PaddleOCR基础功能 ===")
    
    try:
        from paddleocr import PaddleOCR
        print("✅ PaddleOCR导入成功")
        
        # 测试最基本的初始化
        print("测试PaddleOCR初始化...")
        start_time = time.time()
        
        ocr = PaddleOCR(lang='ch', device='cpu')  # 强制使用CPU避免GPU问题
        
        elapsed = time.time() - start_time
        print(f"✅ PaddleOCR初始化成功 (耗时: {elapsed:.2f}秒)")
        
        # 测试简单图像识别
        import numpy as np
        test_image = np.ones((100, 200, 3), dtype=np.uint8) * 255
        cv2.putText(test_image, 'Test', (50, 50), cv2.FONT_HERSHEY_SIMPLEX, 1, (0, 0, 0), 2)
        
        print("测试OCR识别...")
        start_time = time.time()
        
        result = ocr.ocr(test_image)
        
        elapsed = time.time() - start_time
        print(f"✅ OCR识别完成 (耗时: {elapsed:.2f}秒)")
        print(f"   识别结果: {result}")
        
        return True
        
    except Exception as e:
        print(f"❌ PaddleOCR测试失败: {e}")
        import traceback
        traceback.print_exc()
        return False

def test_video_ocr_with_timeout(video_path, timeout_seconds=30):
    """带超时的视频OCR测试"""
    print(f"\n=== 测试视频OCR (带超时控制) ===")
    
    def ocr_worker():
        """OCR工作线程"""
        try:
            from improved_paddleocr_extractor import PaddleOCRExtractor
            
            print("创建PaddleOCR提取器...")
            extractor = PaddleOCRExtractor(
                preset='subtitle',
                use_gpu=False  # 强制使用CPU
            )
            
            print("开始视频字幕提取...")
            
            # 只处理前几帧进行测试
            def progress_callback(msg):
                print(f"进度: {msg}")
            
            subtitles = extractor.extract_subtitles_from_video(
                video_path=video_path,
                sample_interval=5.0,  # 大间隔采样
                confidence_threshold=0.1,
                progress_callback=progress_callback
            )
            
            print(f"✅ OCR测试完成，识别到 {len(subtitles)} 条字幕")
            return True
            
        except Exception as e:
            print(f"❌ OCR工作线程异常: {e}")
            import traceback
            traceback.print_exc()
            return False
    
    # 创建工作线程
    import threading
    result = [False]
    
    def worker_wrapper():
        result[0] = ocr_worker()
    
    worker_thread = threading.Thread(target=worker_wrapper)
    worker_thread.daemon = True
    
    print(f"启动OCR测试 (超时: {timeout_seconds}秒)...")
    start_time = time.time()
    worker_thread.start()
    
    # 等待完成或超时
    worker_thread.join(timeout_seconds)
    elapsed = time.time() - start_time
    
    if worker_thread.is_alive():
        print(f"⚠️ OCR测试超时 ({elapsed:.2f}秒)，可能发生死锁")
        return False
    else:
        print(f"✅ OCR测试完成 (耗时: {elapsed:.2f}秒)")
        return result[0]

def main():
    """主函数"""
    print("=== OCR死锁问题调试工具 ===\n")
    
    # 查找测试视频文件
    possible_videos = [
        "30584078525-1-192.mp4",  # 用户提到的文件
        "test.mp4",
        "sample.mp4"
    ]
    
    video_path = None
    for video in possible_videos:
        if os.path.exists(video):
            video_path = video
            break
    
    if not video_path:
        print("❌ 找不到测试视频文件")
        print("请将视频文件放在当前目录或指定路径")
        video_path = input("请输入视频文件路径 (或回车跳过视频测试): ").strip()
        if not video_path:
            print("跳过视频测试")
        elif not os.path.exists(video_path):
            print(f"❌ 指定的视频文件不存在: {video_path}")
            video_path = None
    
    # 1. 测试基础视频读取
    if video_path:
        video_ok = test_video_reading(video_path)
    else:
        video_ok = False
        print("⚠️ 跳过视频读取测试")
    
    # 2. 测试PaddleOCR基础功能
    paddleocr_ok = test_paddleocr_basic()
    
    # 3. 如果前两个测试都通过，测试完整的视频OCR
    if video_ok and paddleocr_ok:
        ocr_ok = test_video_ocr_with_timeout(video_path, timeout_seconds=60)
    else:
        ocr_ok = False
        print("⚠️ 跳过完整OCR测试（前置条件不满足）")
    
    # 输出诊断结果
    print(f"\n=== 诊断结果 ===")
    print(f"视频读取: {'✅ 正常' if video_ok else '❌ 异常'}")
    print(f"PaddleOCR: {'✅ 正常' if paddleocr_ok else '❌ 异常'}")
    print(f"完整OCR: {'✅ 正常' if ocr_ok else '❌ 异常'}")
    
    if not ocr_ok:
        print(f"\n=== 可能的问题和解决方案 ===")
        if not video_ok:
            print("• 视频文件读取有问题，检查文件格式和编码")
        if not paddleocr_ok:
            print("• PaddleOCR安装或配置有问题")
        if video_ok and paddleocr_ok and not ocr_ok:
            print("• 可能是OCR处理过程中的死锁问题")
            print("• 建议检查内存使用和线程安全问题")

if __name__ == "__main__":
    main() 