#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
专门针对黑底白字字幕优化的提取工具
"""

import sys
import os
import cv2
import numpy as np

# 添加路径
current_dir = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, current_dir)

def extract_black_bg_subtitles(video_path):
    """专门提取黑底白字字幕"""
    
    print("=== 黑底白字字幕提取器 ===")
    print(f"视频: {video_path}")
    
    try:
        from tools.ocr.improved_ocr_extractor import ImprovedOCRExtractor
        
        # 测试多种配置
        configs = [
            # (置信度阈值, 字幕区域比例, 配置描述)
            (0.1, 0.7, "超低置信度，底部30%"),
            (0.05, 0.7, "极低置信度，底部30%"),
            (0.1, 0.8, "超低置信度，底部20%"),
            (0.05, 0.8, "极低置信度，底部20%"),
            (0.1, 0.6, "超低置信度，底部40%"),
            (0.05, 0.6, "极低置信度，底部40%"),
        ]
        
        best_result = None
        best_count = 0
        
        for i, (confidence, region_ratio, description) in enumerate(configs):
            print(f"\n--- 配置 {i+1}: {description} (置信度={confidence}) ---")
            
            try:
                # 使用GPU版本，专门针对黑底白字优化
                extractor = ImprovedOCRExtractor(
                    languages=['ch', 'en'],
                    use_gpu=True,
                    engine='paddleocr'
                )
                
                # 自定义字幕区域
                cap = cv2.VideoCapture(video_path)
                if cap.isOpened():
                    width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
                    height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
                    y_start = int(height * region_ratio)
                    subtitle_region = (0, y_start, width, height)
                    cap.release()
                else:
                    subtitle_region = None
                
                # 提取字幕 - 使用正确的参数名
                subtitles = extractor.extract_subtitles_from_video(
                    video_path=video_path,
                    sample_interval=2.0,
                    subtitle_region=subtitle_region,
                    confidence_threshold=confidence
                )
                
                subtitle_count = len(subtitles)
                print(f"结果: 找到 {subtitle_count} 条字幕")
                
                if subtitle_count > best_count:
                    best_count = subtitle_count
                    best_result = (subtitles, confidence, region_ratio, description)
                    print(f"🎉 新的最佳结果!")
                
                # 如果找到足够多的字幕，提前结束
                if subtitle_count > 10:
                    print(f"✅ 找到足够多的字幕，使用此配置")
                    break
                    
            except Exception as e:
                print(f"❌ 配置失败: {e}")
                continue
        
        # 输出最佳结果
        if best_result and best_count > 0:
            subtitles, confidence, region_ratio, description = best_result
            print(f"\n🎉 最佳配置结果:")
            print(f"配置: {description}")
            print(f"置信度阈值: {confidence}")
            print(f"字幕区域: 底部{int((1-region_ratio)*100)}%")
            print(f"找到字幕: {len(subtitles)} 条")
            
            # 显示前几条字幕
            print(f"\n前几条字幕:")
            for i, sub in enumerate(subtitles[:10]):
                print(f"  [{sub.get('start_time', 0):.1f}s] {sub.get('text', '')}")
            
            # 保存结果
            import json
            output_file = "black_bg_subtitles_optimized.json"
            with open(output_file, 'w', encoding='utf-8') as f:
                json.dump(subtitles, f, ensure_ascii=False, indent=2)
            print(f"\n💾 结果已保存到: {output_file}")
            
            return True
        else:
            print(f"\n❌ 所有配置都未找到字幕")
            print(f"建议检查:")
            print(f"1. 调试图像 debug_original_*.png 是否包含字幕")
            print(f"2. 字幕是否真的是黑底白字")
            print(f"3. 字幕字体是否足够大和清晰")
            print(f"4. 尝试手动调整字幕区域")
            
            return False
            
    except Exception as e:
        print(f"❌ 提取失败: {e}")
        import traceback
        traceback.print_exc()
        return False

def test_configuration(video_path, confidence, subtitle_region, config_name):
    """测试单个配置"""
    print(f"\n--- 配置 {config_name} ---")
    
    try:
        # 创建提取器，移除不支持的参数
        extractor = ImprovedOCRExtractor(
            languages=['ch', 'en'],
            use_gpu=True,
            engine='paddleocr'
        )
        
        # 提取字幕
        subtitles = extractor.extract_subtitles_from_video(
            video_path,
            sample_interval=1.0,  # 更频繁的采样
            subtitle_region=subtitle_region,
            confidence_threshold=confidence,
            progress_callback=lambda msg: print(f"    {msg}")
        )
        
        result_info = f"识别到 {len(subtitles)} 条字幕"
        print(f"✅ {config_name}: {result_info}")
        
        # 显示前几条字幕
        if subtitles:
            print("前几条字幕:")
            for i, sub in enumerate(subtitles[:3]):
                print(f"  {i+1}. [{sub['start_time']:.1f}s-{sub['end_time']:.1f}s] {sub['text']}")
        
        return len(subtitles), subtitles
        
    except Exception as e:
        print(f"❌ 配置失败: {e}")
        return 0, []

def main():
    """主函数"""
    if len(sys.argv) != 2:
        print("使用方法: python optimized_black_subtitle.py <视频文件路径>")
        return
    
    video_path = sys.argv[1]
    if not os.path.exists(video_path):
        print(f"❌ 视频文件不存在: {video_path}")
        return
    
    success = extract_black_bg_subtitles(video_path)
    
    if success:
        print(f"\n🎉 黑底白字字幕提取成功!")
    else:
        print(f"\n💡 提示: 请检查调试图像，确认字幕位置和清晰度")

if __name__ == "__main__":
    main() 