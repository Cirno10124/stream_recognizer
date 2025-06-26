@echo off
echo ================================================================
echo                   Silero VAD ONNX 集成设置
echo ================================================================
echo.

REM 检查Python是否安装
python --version >nul 2>&1
if %errorlevel% neq 0 (
    echo ❌ 错误: 未找到Python，请先安装Python 3.7+
    echo 📥 下载地址: https://www.python.org/downloads/
    pause
    exit /b 1
)

echo ✅ Python已安装
python --version

echo.
echo 📦 正在安装Python依赖包...
echo ----------------------------------------------------------------

REM 安装必要的Python包
pip install torch torchvision torchaudio --index-url https://download.pytorch.org/whl/cpu
if %errorlevel% neq 0 (
    echo ❌ PyTorch安装失败，尝试使用默认源...
    pip install torch torchvision torchaudio
)

pip install onnx onnxruntime
if %errorlevel% neq 0 (
    echo ❌ Python包安装失败
    pause
    exit /b 1
)

echo ✅ Python依赖包安装完成

echo.
echo 🤖 正在导出Silero VAD模型...
echo ----------------------------------------------------------------

REM 导出Silero VAD模型
python export_silero_vad.py
if %errorlevel% neq 0 (
    echo ❌ 模型导出失败
    pause
    exit /b 1
)

echo.
echo 📚 正在下载ONNX Runtime C++库...
echo ----------------------------------------------------------------

REM 检查是否已存在ONNX Runtime
if exist "onnxruntime" (
    echo ⚠️  ONNX Runtime目录已存在，跳过下载
    goto :configure
)

echo 📥 请手动下载ONNX Runtime:
echo    1. 访问: https://github.com/microsoft/onnxruntime/releases
echo    2. 下载: onnxruntime-win-x64-1.16.3.zip
echo    3. 解压到当前目录的 onnxruntime 文件夹中
echo.
echo 或者使用vcpkg安装:
echo    vcpkg install onnxruntime:x64-windows
echo.

:configure
echo.
echo 🔧 配置项目...
echo ----------------------------------------------------------------

REM 创建必要的目录
if not exist "models" mkdir models
if not exist "include\silero" mkdir include\silero
if not exist "src\silero" mkdir src\silero

echo ✅ 目录结构创建完成

echo.
echo 📋 下一步需要手动完成:
echo ----------------------------------------------------------------
echo 1. 📁 确保ONNX Runtime已正确安装到 onnxruntime/ 目录
echo 2. 🔨 在Visual Studio项目中添加包含路径:
echo    - onnxruntime/include
echo 3. 🔗 在项目中添加库路径:
echo    - onnxruntime/lib
echo 4. 📚 在项目中链接库文件:
echo    - onnxruntime.lib
echo 5. 🚀 编译并运行项目

echo.
echo ✅ Silero VAD ONNX集成准备完成!
echo 📂 模型文件位置: models/silero_vad.onnx
echo 📖 详细文档: docs/silero_vad_integration_guide.md

pause 