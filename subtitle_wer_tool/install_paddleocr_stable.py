#!/usr/bin/env python3
"""
PaddleOCR 3.0.0 稳定版安装脚本
自动检测并安装稳定版本
"""

import subprocess
import sys
import os
from typing import List, Tuple

def run_command(command: List[str]) -> Tuple[bool, str]:
    """运行命令并返回结果"""
    try:
        result = subprocess.run(
            command, 
            capture_output=True, 
            text=True, 
            check=True
        )
        return True, result.stdout
    except subprocess.CalledProcessError as e:
        return False, e.stderr

def check_current_version():
    """检查当前PaddleOCR版本"""
    try:
        import paddleocr
        version = getattr(paddleocr, '__version__', 'unknown')
        print(f"当前PaddleOCR版本: {version}")
        return version
    except ImportError:
        print("PaddleOCR未安装")
        return None

def install_stable_paddleocr():
    """安装PaddleOCR 3.0.0稳定版"""
    print("正在安装PaddleOCR 3.0.0稳定版...")
    
    # 首先尝试卸载现有版本
    print("1. 卸载现有版本...")
    success, output = run_command([sys.executable, "-m", "pip", "uninstall", "paddleocr", "-y"])
    if success:
        print("   ✅ 卸载成功")
    else:
        print(f"   ⚠️ 卸载失败或未安装: {output}")
    
    # 安装稳定版
    print("2. 安装PaddleOCR 3.0.0稳定版...")
    
    # 使用清华源加速下载
    install_commands = [
        # 先安装基础依赖
        [sys.executable, "-m", "pip", "install", "-i", "https://pypi.tuna.tsinghua.edu.cn/simple", 
         "paddlepaddle-gpu"],
        
        # 安装PaddleOCR稳定版
        [sys.executable, "-m", "pip", "install", "-i", "https://pypi.tuna.tsinghua.edu.cn/simple", 
         "paddleocr==3.0.0"]
    ]
    
    for i, cmd in enumerate(install_commands, 1):
        print(f"   步骤 {i}: {' '.join(cmd[5:])}")  # 只显示包名部分
        success, output = run_command(cmd)
        if success:
            print(f"   ✅ 安装成功")
        else:
            print(f"   ❌ 安装失败: {output}")
            
            # 如果GPU版本失败，尝试CPU版本
            if "paddlepaddle-gpu" in ' '.join(cmd):
                print("   尝试安装CPU版本...")
                cpu_cmd = cmd.copy()
                cpu_cmd[-1] = "paddlepaddle"
                success, output = run_command(cpu_cmd)
                if success:
                    print("   ✅ CPU版本安装成功")
                else:
                    print(f"   ❌ CPU版本也失败: {output}")
                    return False
    
    return True

def verify_installation():
    """验证安装是否成功"""
    print("3. 验证安装...")
    
    try:
        import paddleocr
        from paddleocr import PaddleOCR
        
        version = getattr(paddleocr, '__version__', 'unknown')
        print(f"   版本: {version}")
        
        # 测试初始化
        print("   测试初始化...")
        try:
            ocr = PaddleOCR(
                use_textline_orientation=True,
                lang='ch',
                device='cpu',  # 使用CPU确保兼容性
                use_doc_orientation_classify=False,
                use_doc_unwarping=False
            )
            print("   ✅ 初始化成功")
            return True
        except Exception as e:
            print(f"   ❌ 初始化失败: {e}")
            return False
            
    except ImportError as e:
        print(f"   ❌ 导入失败: {e}")
        return False

def create_requirements_file():
    """创建requirements文件"""
    requirements_content = """# PaddleOCR 3.0.0 稳定版依赖
paddlepaddle-gpu>=2.5.0
paddleocr==3.0.0

# 可选依赖（用于视频处理）
opencv-python>=4.5.0
numpy>=1.21.0
Pillow>=8.0.0

# 字幕处理相关
jiwer>=2.3.0  # WER计算
"""
    
    with open('requirements_paddleocr_stable.txt', 'w', encoding='utf-8') as f:
        f.write(requirements_content)
    
    print("✅ 已创建 requirements_paddleocr_stable.txt")

def main():
    """主函数"""
    print("=== PaddleOCR 3.0.0 稳定版安装脚本 ===")
    
    # 检查当前版本
    current_version = check_current_version()
    
    if current_version == "3.0.0":
        print("✅ 已安装PaddleOCR 3.0.0稳定版")
        return
    
    # 安装稳定版
    if install_stable_paddleocr():
        # 验证安装
        if verify_installation():
            print("\n🎉 PaddleOCR 3.0.0稳定版安装完成!")
            
            # 创建requirements文件
            create_requirements_file()
            
            print("\n下一步:")
            print("1. 可以运行 'python paddleocr_v3_stable.py --show-params' 查看所有参数")
            print("2. 运行 'python paddleocr_v3_stable.py --show-presets' 查看预设配置")
            print("3. 运行 'python paddleocr_v3_stable.py --test-init' 测试初始化")
        else:
            print("\n💥 安装验证失败")
    else:
        print("\n💥 安装失败")

if __name__ == "__main__":
    main()
 