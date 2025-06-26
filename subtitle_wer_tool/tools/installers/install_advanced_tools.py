#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
高级字幕提取工具安装脚本
安装MediaInfo、MKVToolNix等工具
"""

import os
import sys
import subprocess
import platform
import urllib.request
import zipfile
import tempfile
from pathlib import Path


def check_tool_available(tool_name: str, version_arg: str = '--version') -> bool:
    """检查工具是否可用"""
    try:
        result = subprocess.run([tool_name, version_arg], 
                              capture_output=True, text=True, timeout=10)
        return result.returncode == 0
    except (FileNotFoundError, subprocess.TimeoutExpired):
        return False


def install_mediainfo_windows():
    """在Windows上安装MediaInfo"""
    print("正在下载MediaInfo...")
    
    # MediaInfo CLI下载URL
    url = "https://mediaarea.net/download/binary/mediainfo/23.10/MediaInfo_CLI_23.10_Windows_x64.zip"
    
    with tempfile.TemporaryDirectory() as temp_dir:
        zip_path = os.path.join(temp_dir, "mediainfo.zip")
        
        # 下载文件
        urllib.request.urlretrieve(url, zip_path)
        
        # 解压
        with zipfile.ZipFile(zip_path, 'r') as zip_ref:
            zip_ref.extractall(temp_dir)
        
        # 查找可执行文件
        mediainfo_exe = None
        for root, dirs, files in os.walk(temp_dir):
            for file in files:
                if file.lower() == 'mediainfo.exe':
                    mediainfo_exe = os.path.join(root, file)
                    break
            if mediainfo_exe:
                break
        
        if mediainfo_exe:
            # 复制到系统PATH中的位置
            system_path = os.environ.get('SYSTEMROOT', 'C:\\Windows')
            target_path = os.path.join(system_path, 'System32', 'mediainfo.exe')
            
            try:
                import shutil
                shutil.copy2(mediainfo_exe, target_path)
                print(f"MediaInfo已安装到: {target_path}")
                return True
            except PermissionError:
                print("权限不足，请以管理员身份运行此脚本")
                print(f"或手动将 {mediainfo_exe} 复制到PATH中的目录")
                return False
        else:
            print("未找到MediaInfo可执行文件")
            return False


def install_mkvtoolnix_windows():
    """在Windows上安装MKVToolNix"""
    print("正在下载MKVToolNix...")
    
    # MKVToolNix下载URL (便携版)
    url = "https://mkvtoolnix.download/windows/releases/79.0/mkvtoolnix-64-bit-79.0.7z"
    
    print("请手动下载并安装MKVToolNix:")
    print("1. 访问: https://mkvtoolnix.download/downloads.html#windows")
    print("2. 下载Windows版本")
    print("3. 安装后确保mkvextract.exe在PATH中")
    
    return False


def install_python_packages():
    """安装Python依赖包"""
    packages = [
        'pymediainfo',
        'opencv-python'
    ]
    
    for package in packages:
        try:
            print(f"正在安装 {package}...")
            subprocess.run([sys.executable, '-m', 'pip', 'install', package], 
                         check=True)
            print(f"{package} 安装成功")
        except subprocess.CalledProcessError:
            print(f"{package} 安装失败")
            return False
    
    return True


def main():
    """主函数"""
    print("高级字幕提取工具安装程序")
    print("=" * 50)
    
    system = platform.system().lower()
    
    # 检查现有工具
    tools_status = {
        'ffmpeg': check_tool_available('ffmpeg'),
        'ffprobe': check_tool_available('ffprobe'),
        'mediainfo': check_tool_available('mediainfo'),
        'mkvextract': check_tool_available('mkvextract')
    }
    
    print("当前工具状态:")
    for tool, available in tools_status.items():
        status = "✓ 可用" if available else "✗ 不可用"
        print(f"  {tool}: {status}")
    
    print()
    
    # 安装Python包
    print("安装Python依赖包...")
    if install_python_packages():
        print("Python依赖包安装完成")
    else:
        print("Python依赖包安装失败")
    
    print()
    
    # 根据系统安装工具
    if system == 'windows':
        if not tools_status['mediainfo']:
            print("安装MediaInfo...")
            if install_mediainfo_windows():
                print("MediaInfo安装成功")
            else:
                print("MediaInfo安装失败")
        
        if not tools_status['mkvextract']:
            install_mkvtoolnix_windows()
    
    elif system == 'linux':
        print("Linux系统请使用包管理器安装:")
        print("sudo apt-get install mediainfo mkvtoolnix")
        print("或")
        print("sudo yum install mediainfo mkvtoolnix")
    
    elif system == 'darwin':  # macOS
        print("macOS系统请使用Homebrew安装:")
        print("brew install mediainfo mkvtoolnix")
    
    print()
    print("安装完成！请重新运行字幕提取工具以使用高级功能。")
    
    # 最终检查
    print("\n最终检查:")
    final_tools_status = {
        'ffmpeg': check_tool_available('ffmpeg'),
        'ffprobe': check_tool_available('ffprobe'),
        'mediainfo': check_tool_available('mediainfo'),
        'mkvextract': check_tool_available('mkvextract')
    }
    
    for tool, available in final_tools_status.items():
        status = "✓ 可用" if available else "✗ 不可用"
        print(f"  {tool}: {status}")


if __name__ == '__main__':
    main() 