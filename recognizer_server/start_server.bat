@echo off
echo 正在启动语音识别服务器...

REM 设置CUDA环境变量以解决内存池问题
set CUDA_LAUNCH_BLOCKING=1
set CUDA_CACHE_DISABLE=1
set GGML_USE_VMM=0
set GGML_CUDA_FORCE_LEGACY_POOL=1
set CUDA_DEVICE_MAX_CONNECTIONS=1
set CUDA_VISIBLE_DEVICES=0
set GGML_LOG_LEVEL=2

echo CUDA环境变量已设置：
echo   CUDA_LAUNCH_BLOCKING=%CUDA_LAUNCH_BLOCKING%
echo   CUDA_CACHE_DISABLE=%CUDA_CACHE_DISABLE%
echo   GGML_USE_VMM=%GGML_USE_VMM%
echo   GGML_CUDA_FORCE_LEGACY_POOL=%GGML_CUDA_FORCE_LEGACY_POOL%
echo   CUDA_DEVICE_MAX_CONNECTIONS=%CUDA_DEVICE_MAX_CONNECTIONS%
echo   CUDA_VISIBLE_DEVICES=%CUDA_VISIBLE_DEVICES%

REM 检查CUDA设备状态
echo.
echo 检查CUDA设备状态...
where nvidia-smi >nul 2>&1
if %errorlevel% == 0 (
    nvidia-smi --query-gpu=name,memory.total,memory.used,memory.free --format=csv,noheader,nounits
) else (
    echo 警告: nvidia-smi 未找到，无法检查GPU状态
)

REM 检查必要的文件
echo.
echo 检查必要文件...

set CONFIG_FILE=..\config.json
if not exist "%CONFIG_FILE%" (
    echo 警告: 配置文件不存在: %CONFIG_FILE%
    echo 将使用默认配置
)

set BUILD_DIR=.\build
if not exist "%BUILD_DIR%" (
    echo 错误: 构建目录不存在: %BUILD_DIR%
    echo 请先运行 cmake 构建项目
    pause
    exit /b 1
)

set EXECUTABLE=%BUILD_DIR%\recognizer_server.exe
if not exist "%EXECUTABLE%" (
    echo 错误: 可执行文件不存在: %EXECUTABLE%
    echo 请先编译项目
    pause
    exit /b 1
)

REM 创建必要的目录
echo.
echo 创建必要目录...
if not exist logs mkdir logs
if not exist storage mkdir storage
if not exist temp mkdir temp

REM 清理旧的临时文件
echo 清理临时文件...
del /q storage\tmp_* 2>nul
del /q temp\* 2>nul

echo.
echo 启动服务器...
echo 如果遇到CUDA内存池错误，服务器会自动切换到CPU模式
echo 按 Ctrl+C 停止服务器
echo.

REM 启动服务器
"%EXECUTABLE%" --config "%CONFIG_FILE%"

pause 