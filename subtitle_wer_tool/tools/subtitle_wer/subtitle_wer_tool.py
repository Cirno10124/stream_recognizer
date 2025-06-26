#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
视频字幕提取和WER计算工具
功能：
1. 从视频文件中提取内嵌字幕
2. 计算识别结果与字幕之间的WER（Word Error Rate）
"""

import os
import re
import json
import argparse
import subprocess
from pathlib import Path
from typing import List, Tuple, Optional
import tempfile


class SubtitleExtractor:
    """视频字幕提取器"""
    
    def __init__(self):
        self.temp_dir = tempfile.mkdtemp()
    
    def extract_subtitles(self, video_path: str, output_path: Optional[str] = None) -> List[str]:
        """从视频文件中提取字幕，支持多种提取方法"""
        if not os.path.exists(video_path):
            raise FileNotFoundError(f"视频文件不存在: {video_path}")
        
        # 确定输出目录
        if output_path:
            output_dir = str(Path(output_path).parent)
        else:
            output_dir = self.temp_dir
        
        # 首先尝试使用高级字幕提取器
        try:
            from advanced_subtitle_extractor import AdvancedSubtitleExtractor
            advanced_extractor = AdvancedSubtitleExtractor()
            subtitle_files = advanced_extractor.extract_subtitles(video_path, output_dir)
            
            if subtitle_files:
                print(f"高级提取器成功提取 {len(subtitle_files)} 个字幕文件")
                return subtitle_files
        except ImportError:
            print("高级字幕提取器不可用，使用基础方法")
        except Exception as e:
            print(f"高级提取器失败: {e}，使用基础方法")
        
        # 回退到基础方法
        return self._extract_with_basic_method(video_path, output_path)
    
    def _extract_with_basic_method(self, video_path: str, output_path: Optional[str] = None) -> List[str]:
        """使用基础方法提取字幕"""
        # 检查视频中的字幕流
        subtitle_info = self._get_subtitle_streams(video_path)
        if not subtitle_info:
            print("视频中未找到字幕流")
            return []
        
        print(f"找到 {len(subtitle_info)} 个字幕流:")
        for i, info in enumerate(subtitle_info):
            print(f"  流 {i}: {info['codec_name']} - {info.get('tags', {}).get('language', '未知语言')}")
        
        # 提取所有字幕流
        subtitle_files = []
        for i, info in enumerate(subtitle_info):
            if output_path:
                base_name = Path(output_path).stem
                ext = self._get_subtitle_extension(info['codec_name'])
                sub_file = f"{base_name}_stream_{i}{ext}"
            else:
                base_name = Path(video_path).stem
                ext = self._get_subtitle_extension(info['codec_name'])
                sub_file = os.path.join(self.temp_dir, f"{base_name}_stream_{i}{ext}")
            
            # 使用ffmpeg提取字幕
            cmd = [
                'ffmpeg', 
                '-i', video_path,
                '-map', f'0:s:{i}',
                '-c:s', 'copy' if info['codec_name'] in ['srt', 'ass', 'ssa'] else 'srt',
                '-y',
                sub_file
            ]
            
            try:
                result = subprocess.run(cmd, capture_output=True, text=True, encoding='utf-8')
                if result.returncode == 0:
                    subtitle_files.append(sub_file)
                    print(f"成功提取字幕流 {i} 到: {sub_file}")
                else:
                    print(f"提取字幕流 {i} 失败: {result.stderr}")
            except Exception as e:
                print(f"提取字幕流 {i} 时发生错误: {e}")
        
        return subtitle_files
    
    def _get_subtitle_streams(self, video_path: str) -> List[dict]:
        """获取视频中的字幕流信息"""
        cmd = [
            'ffprobe',
            '-v', 'quiet',
            '-print_format', 'json',
            '-show_streams',
            '-select_streams', 's',
            video_path
        ]
        
        try:
            result = subprocess.run(cmd, capture_output=True, text=True, encoding='utf-8')
            if result.returncode == 0:
                data = json.loads(result.stdout)
                return data.get('streams', [])
        except Exception as e:
            print(f"获取字幕流信息时发生错误: {e}")
        
        return []
    
    def _get_subtitle_extension(self, codec_name: str) -> str:
        """根据编解码器名称获取字幕文件扩展名"""
        codec_ext_map = {
            'subrip': '.srt',
            'srt': '.srt',
            'ass': '.ass',
            'ssa': '.ssa',
            'webvtt': '.vtt',
            'mov_text': '.srt',
            'dvd_subtitle': '.sub',
            'hdmv_pgs_subtitle': '.sup'
        }
        return codec_ext_map.get(codec_name.lower(), '.srt')


class SubtitleParser:
    """字幕解析器"""
    
    @staticmethod
    def parse_srt(file_path: str) -> List[str]:
        """解析SRT格式字幕文件"""
        text_content = []
        try:
            with open(file_path, 'r', encoding='utf-8') as f:
                content = f.read()
            
            # SRT格式解析
            pattern = r'\d+\n\d{2}:\d{2}:\d{2},\d{3} --> \d{2}:\d{2}:\d{2},\d{3}\n(.*?)(?=\n\d+\n|\n*$)'
            matches = re.findall(pattern, content, re.DOTALL)
            
            for match in matches:
                # 移除HTML标签和格式化标记
                text = re.sub(r'<[^>]+>', '', match)
                text = re.sub(r'\{[^}]*\}', '', text)
                text = text.strip().replace('\n', ' ')
                if text:
                    text_content.append(text)
                    
        except Exception as e:
            print(f"解析SRT文件时发生错误: {e}")
        
        return text_content
    
    @staticmethod
    def parse_ass(file_path: str) -> List[str]:
        """解析ASS/SSA格式字幕文件"""
        text_content = []
        try:
            with open(file_path, 'r', encoding='utf-8') as f:
                lines = f.readlines()
            
            in_events = False
            for line in lines:
                line = line.strip()
                if line.startswith('[Events]'):
                    in_events = True
                    continue
                elif line.startswith('[') and in_events:
                    break
                elif in_events and line.startswith('Dialogue:'):
                    # ASS格式: Dialogue: Layer,Start,End,Style,Name,MarginL,MarginR,MarginV,Effect,Text
                    parts = line.split(',', 9)
                    if len(parts) >= 10:
                        text = parts[9]
                        # 移除ASS格式标记
                        text = re.sub(r'\{[^}]*\}', '', text)
                        text = re.sub(r'\\N', ' ', text)
                        text = text.strip()
                        if text:
                            text_content.append(text)
                            
        except Exception as e:
            print(f"解析ASS文件时发生错误: {e}")
        
        return text_content
    
    @staticmethod
    def parse_subtitle_file(file_path: str) -> List[str]:
        """自动识别并解析字幕文件"""
        ext = Path(file_path).suffix.lower()
        
        if ext == '.srt':
            return SubtitleParser.parse_srt(file_path)
        elif ext in ['.ass', '.ssa']:
            return SubtitleParser.parse_ass(file_path)
        else:
            # 尝试作为SRT解析
            return SubtitleParser.parse_srt(file_path)


class WERCalculator:
    """WER（Word Error Rate）计算器"""
    
    @staticmethod
    def calculate_wer(reference: str, hypothesis: str) -> Tuple[float, dict]:
        """计算WER"""
        # 预处理文本
        ref_words = WERCalculator._preprocess_text(reference)
        hyp_words = WERCalculator._preprocess_text(hypothesis)
        
        # 计算编辑距离
        distances = WERCalculator._edit_distance(ref_words, hyp_words)
        
        # 统计操作数
        substitutions = 0
        deletions = 0
        insertions = 0
        
        i, j = len(ref_words), len(hyp_words)
        while i > 0 or j > 0:
            if i > 0 and j > 0 and distances[i][j] == distances[i-1][j-1] + (0 if ref_words[i-1] == hyp_words[j-1] else 1):
                if ref_words[i-1] != hyp_words[j-1]:
                    substitutions += 1
                i -= 1
                j -= 1
            elif i > 0 and distances[i][j] == distances[i-1][j] + 1:
                deletions += 1
                i -= 1
            else:
                insertions += 1
                j -= 1
        
        # 计算WER
        total_words = len(ref_words)
        if total_words == 0:
            wer = 0.0 if len(hyp_words) == 0 else 1.0
        else:
            wer = (substitutions + deletions + insertions) / total_words
        
        stats = {
            'wer': wer,
            'substitutions': substitutions,
            'deletions': deletions,
            'insertions': insertions,
            'total_words': total_words,
            'correct_words': total_words - substitutions - deletions
        }
        
        return wer, stats
    
    @staticmethod
    def _preprocess_text(text: str) -> List[str]:
        """预处理文本，转换为单词列表"""
        # 转换为小写
        text = text.lower()
        # 移除标点符号，保留中文字符
        text = re.sub(r'[^\w\s\u4e00-\u9fff]', '', text)
        # 分词（简单按空格分割，对中文按字符分割）
        words = []
        for word in text.split():
            if re.search(r'[\u4e00-\u9fff]', word):
                # 中文按字符分割
                words.extend(list(word))
            else:
                # 英文单词
                words.append(word)
        
        return [w for w in words if w.strip()]
    
    @staticmethod
    def _edit_distance(ref_words: List[str], hyp_words: List[str]) -> List[List[int]]:
        """计算编辑距离矩阵"""
        m, n = len(ref_words), len(hyp_words)
        dp = [[0] * (n + 1) for _ in range(m + 1)]
        
        # 初始化
        for i in range(m + 1):
            dp[i][0] = i
        for j in range(n + 1):
            dp[0][j] = j
        
        # 填充矩阵
        for i in range(1, m + 1):
            for j in range(1, n + 1):
                if ref_words[i-1] == hyp_words[j-1]:
                    dp[i][j] = dp[i-1][j-1]
                else:
                    dp[i][j] = min(
                        dp[i-1][j] + 1,      # 删除
                        dp[i][j-1] + 1,      # 插入
                        dp[i-1][j-1] + 1     # 替换
                    )
        
        return dp


class SubtitleWERTool:
    """字幕WER工具主类"""
    
    def __init__(self):
        self.extractor = SubtitleExtractor()
        self.parser = SubtitleParser()
        self.wer_calculator = WERCalculator()
    
    def extract_and_save_subtitles(self, video_path: str, output_dir: str = None) -> List[str]:
        """提取视频字幕并保存"""
        if output_dir and not os.path.exists(output_dir):
            os.makedirs(output_dir)
        
        output_path = os.path.join(output_dir, Path(video_path).stem) if output_dir else None
        return self.extractor.extract_subtitles(video_path, output_path)
    
    def calculate_wer_from_files(self, subtitle_file: str, recognition_file: str) -> Tuple[float, dict]:
        """从文件计算WER"""
        # 读取字幕文件
        subtitle_texts = self.parser.parse_subtitle_file(subtitle_file)
        subtitle_text = ' '.join(subtitle_texts)
        
        # 读取识别结果文件
        try:
            with open(recognition_file, 'r', encoding='utf-8') as f:
                recognition_text = f.read().strip()
        except Exception as e:
            raise Exception(f"读取识别结果文件失败: {e}")
        
        return self.wer_calculator.calculate_wer(subtitle_text, recognition_text)
    
    def calculate_wer_from_text(self, subtitle_text: str, recognition_text: str) -> Tuple[float, dict]:
        """从文本计算WER"""
        return self.wer_calculator.calculate_wer(subtitle_text, recognition_text)


def main():
    """主函数"""
    parser = argparse.ArgumentParser(description='视频字幕提取和WER计算工具')
    parser.add_argument('command', choices=['extract', 'wer', 'extract-wer'], 
                       help='命令: extract(提取字幕), wer(计算WER), extract-wer(提取字幕并计算WER)')
    parser.add_argument('--video', '-v', required=False, help='视频文件路径')
    parser.add_argument('--subtitle', '-s', required=False, help='字幕文件路径')
    parser.add_argument('--recognition', '-r', required=False, help='识别结果文件路径')
    parser.add_argument('--output', '-o', help='输出目录')
    parser.add_argument('--subtitle-text', help='字幕文本（直接输入）')
    parser.add_argument('--recognition-text', help='识别结果文本（直接输入）')
    
    args = parser.parse_args()
    
    tool = SubtitleWERTool()
    
    try:
        if args.command == 'extract':
            if not args.video:
                print("错误: 提取字幕需要指定视频文件路径 (--video)")
                return
            
            subtitle_files = tool.extract_and_save_subtitles(args.video, args.output)
            if subtitle_files:
                print(f"\n成功提取 {len(subtitle_files)} 个字幕文件:")
                for file in subtitle_files:
                    print(f"  {file}")
            else:
                print("未提取到字幕文件")
        
        elif args.command == 'wer':
            if args.subtitle_text and args.recognition_text:
                # 使用直接输入的文本
                wer, stats = tool.calculate_wer_from_text(args.subtitle_text, args.recognition_text)
            elif args.subtitle and args.recognition:
                # 使用文件
                wer, stats = tool.calculate_wer_from_files(args.subtitle, args.recognition)
            else:
                print("错误: 计算WER需要指定字幕和识别结果文件路径，或直接输入文本")
                return
            
            print(f"\nWER计算结果:")
            print(f"  WER: {wer:.4f} ({wer*100:.2f}%)")
            print(f"  总词数: {stats['total_words']}")
            print(f"  正确词数: {stats['correct_words']}")
            print(f"  替换: {stats['substitutions']}")
            print(f"  删除: {stats['deletions']}")
            print(f"  插入: {stats['insertions']}")
        
        elif args.command == 'extract-wer':
            if not args.video or not args.recognition:
                print("错误: 提取字幕并计算WER需要指定视频文件和识别结果文件路径")
                return
            
            # 提取字幕
            subtitle_files = tool.extract_and_save_subtitles(args.video, args.output)
            if not subtitle_files:
                print("未提取到字幕文件，无法计算WER")
                return
            
            # 使用第一个字幕文件计算WER
            subtitle_file = subtitle_files[0]
            print(f"\n使用字幕文件: {subtitle_file}")
            
            wer, stats = tool.calculate_wer_from_files(subtitle_file, args.recognition)
            
            print(f"\nWER计算结果:")
            print(f"  WER: {wer:.4f} ({wer*100:.2f}%)")
            print(f"  总词数: {stats['total_words']}")
            print(f"  正确词数: {stats['correct_words']}")
            print(f"  替换: {stats['substitutions']}")
            print(f"  删除: {stats['deletions']}")
            print(f"  插入: {stats['insertions']}")
    
    except Exception as e:
        print(f"错误: {e}")


if __name__ == '__main__':
    main() 