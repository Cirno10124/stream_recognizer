#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
高级字幕提取器
支持多种方法提取视频中的内嵌字幕，包括但不限于FFmpeg
"""

import os
import subprocess
import tempfile
import json
import re
from pathlib import Path
from typing import List, Dict, Optional, Tuple
import logging

# 设置日志
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)


class AdvancedSubtitleExtractor:
    """高级字幕提取器，支持多种提取方法"""
    
    def __init__(self):
        self.available_tools = self._check_available_tools()
        logger.info(f"可用工具: {list(self.available_tools.keys())}")
    
    def _check_available_tools(self) -> Dict[str, bool]:
        """检查可用的提取工具"""
        tools = {}
        
        # 检查FFmpeg
        try:
            result = subprocess.run(['ffmpeg', '-version'], 
                                  capture_output=True, text=True, timeout=10)
            tools['ffmpeg'] = result.returncode == 0
        except (FileNotFoundError, subprocess.TimeoutExpired):
            tools['ffmpeg'] = False
            
        # 检查FFprobe
        try:
            result = subprocess.run(['ffprobe', '-version'], 
                                  capture_output=True, text=True, timeout=10)
            tools['ffprobe'] = result.returncode == 0
        except (FileNotFoundError, subprocess.TimeoutExpired):
            tools['ffprobe'] = False
            
        # 检查MediaInfo
        try:
            result = subprocess.run(['mediainfo', '--version'], 
                                  capture_output=True, text=True, timeout=10)
            tools['mediainfo'] = result.returncode == 0
        except (FileNotFoundError, subprocess.TimeoutExpired):
            tools['mediainfo'] = False
            
        # 检查MKVExtract (MKVToolNix)
        try:
            result = subprocess.run(['mkvextract', '--version'], 
                                  capture_output=True, text=True, timeout=10)
            tools['mkvextract'] = result.returncode == 0
        except (FileNotFoundError, subprocess.TimeoutExpired):
            tools['mkvextract'] = False
            
        return tools
    
    def extract_subtitles(self, video_path: str, output_dir: Optional[str] = None) -> List[str]:
        """
        使用多种方法提取字幕
        
        Args:
            video_path: 视频文件路径
            output_dir: 输出目录，如果为None则使用临时目录
            
        Returns:
            提取到的字幕文件路径列表
        """
        if not os.path.exists(video_path):
            raise FileNotFoundError(f"视频文件不存在: {video_path}")
            
        if output_dir is None:
            output_dir = tempfile.mkdtemp()
        os.makedirs(output_dir, exist_ok=True)
        
        video_name = Path(video_path).stem
        subtitle_files = []
        
        logger.info(f"开始提取字幕: {Path(video_path).name}")
        
        # 方法1: 使用FFprobe分析 + FFmpeg提取
        if self.available_tools.get('ffprobe') and self.available_tools.get('ffmpeg'):
            try:
                files = self._extract_with_ffmpeg(video_path, output_dir, video_name)
                subtitle_files.extend(files)
                logger.info(f"FFmpeg提取到 {len(files)} 个字幕文件")
            except Exception as e:
                logger.warning(f"FFmpeg提取失败: {e}")
        
        # 方法2: 使用MKVExtract (专门用于MKV文件)
        if self.available_tools.get('mkvextract') and video_path.lower().endswith('.mkv'):
            try:
                files = self._extract_with_mkvextract(video_path, output_dir, video_name)
                subtitle_files.extend(files)
                logger.info(f"MKVExtract提取到 {len(files)} 个字幕文件")
            except Exception as e:
                logger.warning(f"MKVExtract提取失败: {e}")
        
        # 去重
        subtitle_files = list(set(subtitle_files))
        
        logger.info(f"总共提取到 {len(subtitle_files)} 个字幕文件")
        return subtitle_files
    
    def _extract_with_ffmpeg(self, video_path: str, output_dir: str, video_name: str) -> List[str]:
        """使用FFmpeg提取字幕"""
        subtitle_files = []
        
        # 首先用ffprobe分析字幕流
        probe_cmd = [
            'ffprobe', '-v', 'quiet', '-print_format', 'json', 
            '-show_streams', '-select_streams', 's', video_path
        ]
        
        result = subprocess.run(probe_cmd, capture_output=True, text=True)
        if result.returncode != 0:
            return subtitle_files
            
        try:
            probe_data = json.loads(result.stdout)
            streams = probe_data.get('streams', [])
            
            for i, stream in enumerate(streams):
                codec_name = stream.get('codec_name', 'unknown')
                language = stream.get('tags', {}).get('language', 'und')
                title = stream.get('tags', {}).get('title', '')
                
                # 确定文件扩展名
                ext_map = {
                    'subrip': 'srt',
                    'ass': 'ass',
                    'ssa': 'ssa',
                    'webvtt': 'vtt',
                    'mov_text': 'srt',
                    'dvd_subtitle': 'sub',
                    'hdmv_pgs_subtitle': 'sup'
                }
                ext = ext_map.get(codec_name, 'srt')
                
                # 生成输出文件名
                if title:
                    filename = f"{video_name}_{i}_{language}_{title}.{ext}"
                else:
                    filename = f"{video_name}_{i}_{language}.{ext}"
                
                output_path = os.path.join(output_dir, filename)
                
                # 提取字幕
                extract_cmd = [
                    'ffmpeg', '-i', video_path, '-map', f'0:s:{i}', 
                    '-c', 'copy', output_path, '-y'
                ]
                
                result = subprocess.run(extract_cmd, capture_output=True, text=True)
                if result.returncode == 0 and os.path.exists(output_path):
                    subtitle_files.append(output_path)
                    
        except json.JSONDecodeError:
            pass
            
        return subtitle_files
    
    def _extract_with_mkvextract(self, video_path: str, output_dir: str, video_name: str) -> List[str]:
        """使用MKVExtract提取字幕（专门用于MKV文件）"""
        subtitle_files = []
        
        # 获取轨道信息
        info_cmd = ['mkvmerge', '-i', video_path]
        result = subprocess.run(info_cmd, capture_output=True, text=True)
        
        if result.returncode != 0:
            return subtitle_files
            
        # 解析轨道信息
        subtitle_tracks = []
        for line in result.stdout.split('\n'):
            if 'subtitles' in line.lower():
                # 提取轨道ID
                match = re.search(r'Track ID (\d+)', line)
                if match:
                    track_id = match.group(1)
                    
                    # 提取语言和格式信息
                    language = 'und'
                    format_type = 'srt'
                    
                    if 'language:' in line:
                        lang_match = re.search(r'language:(\w+)', line)
                        if lang_match:
                            language = lang_match.group(1)
                    
                    if 'SubRip' in line:
                        format_type = 'srt'
                    elif 'ASS' in line or 'SSA' in line:
                        format_type = 'ass'
                    elif 'VobSub' in line:
                        format_type = 'sub'
                    
                    subtitle_tracks.append((track_id, language, format_type))
        
        # 提取每个字幕轨道
        for track_id, language, format_type in subtitle_tracks:
            filename = f"{video_name}_track{track_id}_{language}.{format_type}"
            output_path = os.path.join(output_dir, filename)
            
            extract_cmd = [
                'mkvextract', 'tracks', video_path, 
                f'{track_id}:{output_path}'
            ]
            
            result = subprocess.run(extract_cmd, capture_output=True, text=True)
            if result.returncode == 0 and os.path.exists(output_path):
                subtitle_files.append(output_path)
                
        return subtitle_files
    
    def get_subtitle_info(self, video_path: str) -> List[Dict]:
        """获取视频中字幕轨道的详细信息"""
        subtitle_info = []
        
        # 使用FFprobe获取详细信息
        if self.available_tools.get('ffprobe'):
            try:
                cmd = [
                    'ffprobe', '-v', 'quiet', '-print_format', 'json',
                    '-show_streams', '-select_streams', 's', video_path
                ]
                
                result = subprocess.run(cmd, capture_output=True, text=True)
                if result.returncode == 0:
                    data = json.loads(result.stdout)
                    streams = data.get('streams', [])
                    
                    for i, stream in enumerate(streams):
                        info = {
                            'index': i,
                            'codec_name': stream.get('codec_name', 'unknown'),
                            'language': stream.get('tags', {}).get('language', 'und'),
                            'title': stream.get('tags', {}).get('title', ''),
                            'default': stream.get('disposition', {}).get('default', 0),
                            'forced': stream.get('disposition', {}).get('forced', 0),
                        }
                        subtitle_info.append(info)
                        
            except Exception as e:
                logger.warning(f"获取字幕信息失败: {e}")
        
        return subtitle_info


def main():
    """测试函数"""
    import argparse
    
    parser = argparse.ArgumentParser(description='高级字幕提取器')
    parser.add_argument('video_path', help='视频文件路径')
    parser.add_argument('--output', '-o', help='输出目录')
    parser.add_argument('--info', action='store_true', help='只显示字幕信息，不提取')
    
    args = parser.parse_args()
    
    extractor = AdvancedSubtitleExtractor()
    
    if args.info:
        # 显示字幕信息
        info = extractor.get_subtitle_info(args.video_path)
        if info:
            print("发现的字幕轨道:")
            for i, track in enumerate(info):
                print(f"  轨道 {i}: {track['codec_name']} ({track['language']}) - {track['title']}")
        else:
            print("未发现字幕轨道")
    else:
        # 提取字幕
        subtitle_files = extractor.extract_subtitles(args.video_path, args.output)
        
        if subtitle_files:
            print(f"成功提取 {len(subtitle_files)} 个字幕文件:")
            for file_path in subtitle_files:
                print(f"  {file_path}")
        else:
            print("未找到可提取的字幕")


if __name__ == '__main__':
    main() 