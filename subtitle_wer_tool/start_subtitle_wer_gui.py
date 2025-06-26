#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
字幕WER计算工具 - 主启动脚本
自动处理路径问题，启动GUI版本
"""

import sys
import os
from pathlib import Path

def setup_paths():
    """设置Python路径，确保能找到所有模块"""
    # 获取脚本所在目录
    script_dir = Path(__file__).parent.absolute()
    
    # 添加工具目录到Python路径
    tools_dirs = [
        script_dir / 'tools' / 'subtitle_wer',
        script_dir / 'tools' / 'ocr',
        script_dir / 'tools' / 'installers',
        script_dir / 'tools' / 'tests'
    ]
    
    for tool_dir in tools_dirs:
        if tool_dir.exists():
            sys.path.insert(0, str(tool_dir))
    
    # 添加根目录到路径（为了兼容性）
    sys.path.insert(0, str(script_dir))

def check_dependencies():
    """检查依赖是否安装"""
    missing_deps = []
    
    try:
        import PyQt5
    except ImportError:
        missing_deps.append('PyQt5')
    
    try:
        import cv2
    except ImportError:
        missing_deps.append('opencv-python')
    
    try:
        import easyocr
    except ImportError:
        missing_deps.append('easyocr')
    
    if missing_deps:
        print("缺少以下依赖包:")
        for dep in missing_deps:
            print(f"  - {dep}")
        print("\n请运行以下命令安装:")
        print("pip install -r docs/wer_tool/requirements_gui.txt")
        return False
    
    return True

def main():
    """主函数"""
    print("字幕WER计算工具启动器")
    print("=" * 40)
    
    # 设置路径
    setup_paths()
    
    # 检查依赖
    if not check_dependencies():
        input("\n按回车键退出...")
        return
    
    # 启动GUI
    try:
        print("正在启动GUI...")
        from subtitle_wer_gui import main as gui_main
        gui_main()
    except ImportError as e:
        print(f"导入错误: {e}")
        print("请确保所有文件都在正确的位置")
        input("\n按回车键退出...")
    except Exception as e:
        print(f"启动失败: {e}")
        input("\n按回车键退出...")

if __name__ == '__main__':
    main() 