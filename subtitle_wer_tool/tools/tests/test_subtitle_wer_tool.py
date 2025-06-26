#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
字幕WER工具测试脚本
"""

import os
import tempfile
from subtitle_wer_tool import SubtitleWERTool, WERCalculator


def test_wer_calculation():
    """测试WER计算功能"""
    print("=== 测试WER计算功能 ===")
    
    calculator = WERCalculator()
    
    # 测试用例1：完全匹配
    ref1 = "今天天气很好"
    hyp1 = "今天天气很好"
    wer1, stats1 = calculator.calculate_wer(ref1, hyp1)
    print(f"测试1 - 完全匹配:")
    print(f"  参考: '{ref1}'")
    print(f"  识别: '{hyp1}'")
    print(f"  WER: {wer1:.4f} ({wer1*100:.2f}%)")
    print(f"  统计: {stats1}\n")
    
    # 测试用例2：部分错误
    ref2 = "今天天气很好"
    hyp2 = "今天天气不错"
    wer2, stats2 = calculator.calculate_wer(ref2, hyp2)
    print(f"测试2 - 部分错误:")
    print(f"  参考: '{ref2}'")
    print(f"  识别: '{hyp2}'")
    print(f"  WER: {wer2:.4f} ({wer2*100:.2f}%)")
    print(f"  统计: {stats2}\n")
    
    # 测试用例3：英文测试
    ref3 = "hello world this is a test"
    hyp3 = "hello world this is test"
    wer3, stats3 = calculator.calculate_wer(ref3, hyp3)
    print(f"测试3 - 英文测试:")
    print(f"  参考: '{ref3}'")
    print(f"  识别: '{hyp3}'")
    print(f"  WER: {wer3:.4f} ({wer3*100:.2f}%)")
    print(f"  统计: {stats3}\n")
    
    # 测试用例4：中英混合
    ref4 = "我在学习machine learning"
    hyp4 = "我在学习机器学习"
    wer4, stats4 = calculator.calculate_wer(ref4, hyp4)
    print(f"测试4 - 中英混合:")
    print(f"  参考: '{ref4}'")
    print(f"  识别: '{hyp4}'")
    print(f"  WER: {wer4:.4f} ({wer4*100:.2f}%)")
    print(f"  统计: {stats4}\n")


def test_subtitle_parsing():
    """测试字幕解析功能"""
    print("=== 测试字幕解析功能 ===")
    
    # 创建测试SRT文件
    srt_content = """1
00:00:01,000 --> 00:00:05,000
这是第一句字幕

2
00:00:06,000 --> 00:00:10,000
这是第二句字幕

3
00:00:11,000 --> 00:00:15,000
Hello world, this is English subtitle
"""
    
    # 写入临时文件
    with tempfile.NamedTemporaryFile(mode='w', suffix='.srt', delete=False, encoding='utf-8') as f:
        f.write(srt_content)
        temp_srt_file = f.name
    
    try:
        from subtitle_wer_tool import SubtitleParser
        
        # 解析SRT文件
        texts = SubtitleParser.parse_subtitle_file(temp_srt_file)
        print(f"解析SRT文件:")
        print(f"  文件: {temp_srt_file}")
        print(f"  解析结果: {texts}")
        print(f"  合并文本: '{' '.join(texts)}'\n")
        
    finally:
        # 清理临时文件
        if os.path.exists(temp_srt_file):
            os.unlink(temp_srt_file)


def test_file_wer_calculation():
    """测试从文件计算WER"""
    print("=== 测试文件WER计算 ===")
    
    # 创建测试字幕文件
    srt_content = """1
00:00:01,000 --> 00:00:05,000
今天天气很好

2
00:00:06,000 --> 00:00:10,000
我们去公园散步
"""
    
    # 创建识别结果文件
    recognition_content = "今天天气不错我们去公园走走"
    
    # 写入临时文件
    with tempfile.NamedTemporaryFile(mode='w', suffix='.srt', delete=False, encoding='utf-8') as f:
        f.write(srt_content)
        temp_srt_file = f.name
    
    with tempfile.NamedTemporaryFile(mode='w', suffix='.txt', delete=False, encoding='utf-8') as f:
        f.write(recognition_content)
        temp_txt_file = f.name
    
    try:
        tool = SubtitleWERTool()
        wer, stats = tool.calculate_wer_from_files(temp_srt_file, temp_txt_file)
        
        print(f"文件WER计算:")
        print(f"  字幕文件: {temp_srt_file}")
        print(f"  识别文件: {temp_txt_file}")
        print(f"  WER: {wer:.4f} ({wer*100:.2f}%)")
        print(f"  统计: {stats}\n")
        
    finally:
        # 清理临时文件
        for temp_file in [temp_srt_file, temp_txt_file]:
            if os.path.exists(temp_file):
                os.unlink(temp_file)


def main():
    """主测试函数"""
    print("字幕WER工具测试\n")
    
    try:
        test_wer_calculation()
        test_subtitle_parsing()
        test_file_wer_calculation()
        print("=== 所有测试完成 ===")
        
    except Exception as e:
        print(f"测试过程中发生错误: {e}")


if __name__ == '__main__':
    main() 