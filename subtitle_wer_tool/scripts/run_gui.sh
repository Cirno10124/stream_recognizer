#!/bin/bash

echo "字幕WER计算工具 - GUI版本启动器"
echo "==================================="
echo

# 检查Python
echo "检查Python环境..."
if ! command -v python3 &> /dev/null; then
    if ! command -v python &> /dev/null; then
        echo "错误：未找到Python，请先安装Python 3.6+"
        exit 1
    else
        PYTHON_CMD=python
    fi
else
    PYTHON_CMD=python3
fi

# 检查Python版本
PYTHON_VERSION=$($PYTHON_CMD --version 2>&1 | grep -o '[0-9]\+\.[0-9]\+')
MAJOR_VERSION=$(echo $PYTHON_VERSION | cut -d. -f1)
MINOR_VERSION=$(echo $PYTHON_VERSION | cut -d. -f2)

if [ "$MAJOR_VERSION" -lt 3 ] || ([ "$MAJOR_VERSION" -eq 3 ] && [ "$MINOR_VERSION" -lt 6 ]); then
    echo "错误：Python版本过低（$PYTHON_VERSION），需要3.6或更高版本"
    exit 1
fi

echo "Python版本: $($PYTHON_CMD --version)"
echo

# 检查依赖
echo "检查Python依赖..."
if ! $PYTHON_CMD -c "from PyQt5.QtWidgets import QApplication" 2>/dev/null; then
    echo "警告：未找到PyQt5，正在尝试安装依赖..."
    $PYTHON_CMD -m pip install -r requirements_gui.txt
    if [ $? -ne 0 ]; then
        echo "错误：依赖安装失败，请手动安装"
        echo "运行命令：$PYTHON_CMD -m pip install -r requirements_gui.txt"
        exit 1
    fi
else
    echo "检查OCR依赖..."
    if ! $PYTHON_CMD -c "import cv2, easyocr" 2>/dev/null; then
        echo "警告：未找到OCR依赖，硬字幕识别功能可能无法使用"
        echo "如需完整功能，请运行：$PYTHON_CMD -m pip install -r requirements_gui.txt"
    fi
fi

echo "依赖检查通过"
echo

# 检查FFmpeg
echo "检查FFmpeg..."
if ! command -v ffmpeg &> /dev/null; then
    echo "警告：未找到FFmpeg，字幕提取功能可能无法使用"
    echo "请安装FFmpeg："
    if [[ "$OSTYPE" == "darwin"* ]]; then
        echo "  macOS: brew install ffmpeg"
    elif [[ "$OSTYPE" == "linux-gnu"* ]]; then
        echo "  Ubuntu/Debian: sudo apt install ffmpeg"
        echo "  CentOS/RHEL: sudo yum install ffmpeg"
    fi
    echo
    echo "按Enter键继续启动GUI（跳过FFmpeg检查）..."
    read
else
    echo "FFmpeg 检查通过"
fi

echo
echo "启动GUI应用程序..."
echo

$PYTHON_CMD subtitle_wer_gui.py

if [ $? -ne 0 ]; then
    echo
    echo "程序启动失败，请检查错误信息"
fi 