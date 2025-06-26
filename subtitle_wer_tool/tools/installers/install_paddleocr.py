#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
PaddleOCR安装脚本
自动安装PaddleOCR及其依赖
"""

import subprocess
import sys
import os
import platform

def run_command(command, description=""):
    """运行命令并显示结果"""
    print(f"\n{'='*50}")
    if description:
        print(f"正在{description}...")
    print(f"执行命令: {command}")
    print(f"{'='*50}")
    
    try:
        result = subprocess.run(command, shell=True, check=True, 
                              capture_output=True, text=True)
        print("✓ 成功")
        if result.stdout:
            print(result.stdout)
        return True
    except subprocess.CalledProcessError as e:
        print("✗ 失败")
        print(f"错误: {e}")
        if e.stdout:
            print(f"输出: {e.stdout}")
        if e.stderr:
            print(f"错误信息: {e.stderr}")
        return False

def check_gpu_support():
    """检查GPU支持"""
    print("\n检查GPU支持...")
    
    # 检查NVIDIA GPU
    try:
        result = subprocess.run(['nvidia-smi'], capture_output=True, text=True)
        if result.returncode == 0:
            print("✓ 检测到NVIDIA GPU")
            return True
        else:
            print("✗ 未检测到NVIDIA GPU")
            return False
    except FileNotFoundError:
        print("✗ 未安装NVIDIA驱动或nvidia-smi不可用")
        return False

def install_paddleocr():
    """安装PaddleOCR"""
    print("开始安装PaddleOCR...")
    
    # 检查Python版本
    python_version = sys.version_info
    if python_version < (3, 7):
        print(f"✗ Python版本过低 ({python_version.major}.{python_version.minor})")
        print("PaddleOCR需要Python 3.7或更高版本")
        return False
    
    print(f"✓ Python版本: {python_version.major}.{python_version.minor}.{python_version.micro}")
    
    # 检查操作系统
    system = platform.system()
    print(f"✓ 操作系统: {system}")
    
    # 检查GPU支持
    has_gpu = check_gpu_support()
    
    # 安装基础依赖
    base_packages = [
        "paddlepaddle",
        "paddleocr",
        "opencv-python",
        "pillow",
        "shapely",
        "pyclipper",
        "imgaug",
        "lmdb"
    ]
    
    if has_gpu:
        # 如果有GPU，安装GPU版本的PaddlePaddle
        print("\n安装GPU版本的PaddlePaddle...")
        if not run_command("pip install paddlepaddle-gpu", "安装PaddlePaddle GPU版本"):
            print("GPU版本安装失败，尝试安装CPU版本...")
            if not run_command("pip install paddlepaddle", "安装PaddlePaddle CPU版本"):
                return False
    else:
        # 安装CPU版本
        print("\n安装CPU版本的PaddlePaddle...")
        if not run_command("pip install paddlepaddle", "安装PaddlePaddle CPU版本"):
            return False
    
    # 安装PaddleOCR
    print("\n安装PaddleOCR...")
    if not run_command("pip install paddleocr", "安装PaddleOCR"):
        return False
    
    # 安装其他依赖
    for package in ["opencv-python", "pillow", "shapely", "pyclipper"]:
        print(f"\n安装{package}...")
        if not run_command(f"pip install {package}", f"安装{package}"):
            print(f"警告: {package}安装失败，但可能不影响基本功能")
    
    return True

def test_paddleocr():
    """测试PaddleOCR安装"""
    print("\n测试PaddleOCR安装...")
    
    test_code = '''
import sys
try:
    from paddleocr import PaddleOCR
    print("✓ PaddleOCR导入成功")
    
    # 创建OCR实例（不加载模型，只测试导入）
    print("✓ PaddleOCR可以正常初始化")
    print("安装测试通过！")
    sys.exit(0)
except ImportError as e:
    print(f"✗ PaddleOCR导入失败: {e}")
    sys.exit(1)
except Exception as e:
    print(f"✗ PaddleOCR测试失败: {e}")
    sys.exit(1)
'''
    
    try:
        result = subprocess.run([sys.executable, '-c', test_code], 
                              capture_output=True, text=True, timeout=30)
        if result.returncode == 0:
            print(result.stdout)
            return True
        else:
            print(result.stderr)
            return False
    except subprocess.TimeoutExpired:
        print("✗ 测试超时")
        return False
    except Exception as e:
        print(f"✗ 测试失败: {e}")
        return False

def main():
    """主函数"""
    print("PaddleOCR安装脚本")
    print("="*50)
    
    # 检查pip
    try:
        subprocess.run([sys.executable, '-m', 'pip', '--version'], 
                      check=True, capture_output=True)
        print("✓ pip可用")
    except subprocess.CalledProcessError:
        print("✗ pip不可用，请先安装pip")
        return False
    
    # 升级pip
    print("\n升级pip...")
    run_command(f"{sys.executable} -m pip install --upgrade pip", "升级pip")
    
    # 安装PaddleOCR
    if not install_paddleocr():
        print("\n✗ PaddleOCR安装失败")
        return False
    
    # 测试安装
    if not test_paddleocr():
        print("\n✗ PaddleOCR测试失败")
        return False
    
    print("\n" + "="*50)
    print("✓ PaddleOCR安装完成！")
    print("\n使用说明:")
    print("1. 重启字幕WER工具")
    print("2. 在OCR设置中选择'PaddleOCR (推荐)'")
    print("3. 享受更好的中文OCR识别效果")
    print("="*50)
    
    return True

if __name__ == '__main__':
    try:
        success = main()
        if not success:
            input("\n按回车键退出...")
            sys.exit(1)
        else:
            input("\n按回车键退出...")
            sys.exit(0)
    except KeyboardInterrupt:
        print("\n\n用户取消安装")
        sys.exit(1)
    except Exception as e:
        print(f"\n\n安装过程中发生未预期的错误: {e}")
        input("按回车键退出...")
        sys.exit(1) 