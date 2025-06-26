@echo off
chcp 65001 >nul
echo 字幕WER计算工具 - GUI版本启动器
echo ===================================

echo.
echo 检查Python环境...
python --version >nul 2>&1
if %errorlevel% neq 0 (
    echo 错误：未找到Python，请先安装Python 3.6+
    pause
    exit /b 1
)

echo.
echo 检查Python依赖...
python -c "from PyQt5.QtWidgets import QApplication" >nul 2>&1
if %errorlevel% neq 0 (
    echo 警告：未找到PyQt5，正在尝试安装依赖...
    pip install -r requirements_gui.txt
    if %errorlevel% neq 0 (
        echo 错误：依赖安装失败，请手动安装
        echo 运行命令：pip install -r requirements_gui.txt
        pause
        exit /b 1
    )
) else (
    echo 检查OCR依赖...
    python -c "import cv2, easyocr" >nul 2>&1
    if %errorlevel% neq 0 (
        echo 警告：未找到OCR依赖，硬字幕识别功能可能无法使用
        echo 如需完整功能，请运行：pip install -r requirements_gui.txt
    )
)

echo.
echo 检查FFmpeg...
ffmpeg -version >nul 2>&1
if %errorlevel% neq 0 (
    echo 警告：未找到FFmpeg，字幕提取功能可能无法使用
    echo 请安装FFmpeg：
    echo   方法1：choco install ffmpeg
    echo   方法2：从 https://ffmpeg.org/download.html 下载
    echo.
    echo 按任意键继续启动GUI（跳过FFmpeg检查）...
    pause >nul
)

echo.
echo 启动GUI应用程序...
echo.
python subtitle_wer_gui.py

if %errorlevel% neq 0 (
    echo.
    echo 程序启动失败，请检查错误信息
    pause
) 