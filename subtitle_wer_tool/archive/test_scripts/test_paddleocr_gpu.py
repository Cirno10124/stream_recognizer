#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
测试重新安装的PaddleOCR GPU版本
"""

import os
import cv2
import numpy as np

def test_paddleocr_gpu():
    """测试PaddleOCR GPU版本"""
    print("=== 测试PaddleOCR GPU版本 ===\n")
    
    try:
        # 检查PaddlePaddle版本
        import paddle
        print(f"✅ PaddlePaddle版本: {paddle.__version__}")
        print(f"✅ CUDA可用: {paddle.device.is_compiled_with_cuda()}")
        print(f"✅ GPU数量: {paddle.device.cuda.device_count()}")
        
        # 初始化PaddleOCR
        print("\n初始化PaddleOCR...")
        from paddleocr import PaddleOCR
        
        # 使用GPU初始化
        ocr = PaddleOCR(
            use_textline_orientation=True, 
            lang='ch', 
            device='gpu',  # 使用GPU
            show_log=False
        )
        print("✅ PaddleOCR GPU版本初始化成功!")
        
        # 创建测试图像（黑底白字）
        print("\n创建黑底白字测试图像...")
        img = np.zeros((150, 800, 3), dtype=np.uint8)
        
        # 添加白色字幕文字
        cv2.putText(img, 'Hello World - GPU Test', (50, 60), 
                    cv2.FONT_HERSHEY_SIMPLEX, 1.5, (255, 255, 255), 2)
        cv2.putText(img, '这是GPU版本测试', (50, 120), 
                    cv2.FONT_HERSHEY_SIMPLEX, 1.5, (255, 255, 255), 2)
        
        cv2.imwrite('paddleocr_gpu_test.png', img)
        print("✅ 测试图像已保存: paddleocr_gpu_test.png")
        
        # OCR识别测试
        print("\n开始OCR识别测试...")
        results = ocr.ocr(img)
        
        print(f"OCR结果: {results}")
        
        if results and results[0]:
            print("\n✅ OCR识别成功!")
            for line in results[0]:
                if line and len(line) >= 2:
                    text_info = line[1]
                    if isinstance(text_info, (list, tuple)) and len(text_info) >= 2:
                        text, confidence = text_info[0], text_info[1]
                        print(f"  识别文本: '{text}' (置信度: {confidence:.3f})")
            return True
        else:
            print("❌ OCR未识别到文本")
            return False
            
    except Exception as e:
        print(f"❌ 测试失败: {e}")
        import traceback
        traceback.print_exc()
        return False

def test_video_ocr(video_path):
    """测试视频OCR功能"""
    if not os.path.exists(video_path):
        print(f"❌ 视频文件不存在: {video_path}")
        return False
    
    print(f"\n=== 测试视频OCR: {video_path} ===")
    
    try:
        from paddleocr import PaddleOCR
        
        # 使用GPU版本
        ocr = PaddleOCR(
            use_textline_orientation=True, 
            lang='ch', 
            device='gpu',
            show_log=False
        )
        
        # 打开视频
        cap = cv2.VideoCapture(video_path)
        if not cap.isOpened():
            print("❌ 无法打开视频")
            return False
        
        # 获取视频信息
        width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
        height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
        fps = cap.get(cv2.CAP_PROP_FPS)
        
        print(f"视频信息: {width}x{height}, {fps:.1f}fps")
        
        # 跳转到中间帧
        frame_num = int(fps * 30)  # 30秒处
        cap.set(cv2.CAP_PROP_POS_FRAMES, frame_num)
        ret, frame = cap.read()
        
        if not ret:
            print("❌ 无法读取视频帧")
            cap.release()
            return False
        
        # 提取字幕区域（底部30%）
        y_start = int(height * 0.7)
        subtitle_region = frame[y_start:, 0:width]
        
        # 保存测试帧
        cv2.imwrite('video_test_frame.png', frame)
        cv2.imwrite('video_subtitle_region.png', subtitle_region)
        print("💾 测试帧已保存: video_test_frame.png, video_subtitle_region.png")
        
        # OCR识别
        print("开始OCR识别...")
        results = ocr.ocr(subtitle_region)
        
        if results and results[0]:
            print("✅ 在视频中识别到文本:")
            for line in results[0]:
                if line and len(line) >= 2:
                    text_info = line[1]
                    if isinstance(text_info, (list, tuple)) and len(text_info) >= 2:
                        text, confidence = text_info[0], text_info[1]
                        print(f"  '{text}' (置信度: {confidence:.3f})")
            cap.release()
            return True
        else:
            print("❌ 未识别到字幕文本")
            print("建议检查:")
            print("1. 字幕区域是否正确")
            print("2. 字幕是否清晰")
            print("3. 尝试不同的时间点")
            cap.release()
            return False
            
    except Exception as e:
        print(f"❌ 视频OCR测试失败: {e}")
        return False

def main():
    """主函数"""
    import sys
    
    # 先测试基本OCR功能
    basic_test_passed = test_paddleocr_gpu()
    
    if basic_test_passed:
        print("\n🎉 PaddleOCR GPU版本工作正常!")
        
        # 如果提供了视频路径，测试视频OCR
        if len(sys.argv) > 1:
            video_path = sys.argv[1]
            video_test_passed = test_video_ocr(video_path)
            
            if video_test_passed:
                print(f"\n🎉 视频OCR测试成功! 现在可以使用GPU版本提取字幕了")
                print(f"运行命令: python tools/ocr/improved_ocr_extractor.py \"{video_path}\" --confidence 0.2")
            else:
                print(f"\n⚠️ 视频OCR测试未完全成功，但基本功能正常")
                print("可以尝试运行完整的字幕提取工具")
        else:
            print("\n要测试视频OCR，请运行:")
            print("python test_paddleocr_gpu.py <视频文件路径>")
    else:
        print("\n❌ PaddleOCR GPU版本有问题，可能需要:")
        print("1. 检查CUDA驱动是否正确安装")
        print("2. 检查GPU是否支持CUDA")
        print("3. 尝试使用CPU版本: device='cpu'")

if __name__ == "__main__":
    main() 