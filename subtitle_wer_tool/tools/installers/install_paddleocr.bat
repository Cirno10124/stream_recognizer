@echo off
chcp 65001 >nul
echo ========================================
echo PaddleOCR 安装脚本 (Windows)
echo ========================================
echo.

echo 正在启动PaddleOCR安装程序...
echo.

python install_paddleocr.py

echo.
echo 安装完成！
echo.
pause 