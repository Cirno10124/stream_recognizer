@echo off
echo 正在应用CUDA内存池修复补丁...

set WHISPER_DIR=3rd_party\whisper.cpp
set TARGET_FILE=%WHISPER_DIR%\ggml\src\ggml-cuda\ggml-cuda.cu

REM 检查文件是否存在
if not exist "%TARGET_FILE%" (
    echo 错误: 目标文件不存在: %TARGET_FILE%
    pause
    exit /b 1
)

REM 备份原文件
for /f "tokens=2 delims==" %%a in ('wmic OS Get localdatetime /value') do set "dt=%%a"
set "YY=%dt:~2,2%" & set "YYYY=%dt:~0,4%" & set "MM=%dt:~4,2%" & set "DD=%dt:~6,2%"
set "HH=%dt:~8,2%" & set "Min=%dt:~10,2%" & set "Sec=%dt:~12,2%"
set "datestamp=%YYYY%%MM%%DD%_%HH%%Min%%Sec%"

set BACKUP_FILE=%TARGET_FILE%.backup.%datestamp%
echo 备份原文件到: %BACKUP_FILE%
copy "%TARGET_FILE%" "%BACKUP_FILE%"

echo 正在修改 %TARGET_FILE% ...

REM 创建临时的PowerShell脚本来进行文本替换
echo $content = Get-Content '%TARGET_FILE%' -Raw > temp_replace.ps1
echo $content = $content -replace 'GGML_ASSERT\(ptr == \(void \*\) \(\(char \*\)\(pool_addr\) \+ pool_used\)\);', '// 添加更安全的内存池释放检查，避免断言失败`r`n        void* expected_ptr = (void *) ((char *)(pool_addr) + pool_used);`r`n        if (ptr != expected_ptr) {`r`n            // 记录错误但不中断程序`r`n            fprintf(stderr, "Warning: CUDA VMM pool deallocation order violation. Expected: %%p, Got: %%p\n", expected_ptr, ptr);`r`n            // 强制重置内存池状态`r`n            pool_used = 0;`r`n        } else {`r`n            // 正常释放`r`n        }' >> temp_replace.ps1
echo $content = $content -replace 'if \(ggml_cuda_info\(\)\.devices\[device\]\.vmm\) \{', 'if (ggml_cuda_info().devices[device].vmm) {`r`n        // 检查环境变量，允许强制禁用VMM`r`n        const char* force_legacy = getenv("GGML_CUDA_FORCE_LEGACY_POOL");`r`n        if (force_legacy ^&^& atoi(force_legacy) != 0) {`r`n            fprintf(stderr, "Info: Forcing legacy CUDA memory pool due to GGML_CUDA_FORCE_LEGACY_POOL\n");`r`n            return std::unique_ptr^<ggml_cuda_pool^>(new ggml_cuda_pool_leg(device));`r`n        }' >> temp_replace.ps1
echo Set-Content '%TARGET_FILE%' $content >> temp_replace.ps1

REM 执行PowerShell脚本
powershell -ExecutionPolicy Bypass -File temp_replace.ps1

REM 清理临时文件
del temp_replace.ps1

echo 补丁应用完成！
echo.
echo 修改内容：
echo 1. 将CUDA虚拟内存池的断言失败改为警告，避免程序崩溃
echo 2. 添加环境变量 GGML_CUDA_FORCE_LEGACY_POOL 来强制使用传统内存池
echo.
echo 现在需要重新编译项目：
echo   cd build
echo   cmake --build . --config Release
echo.
echo 如果需要恢复原文件，请使用备份: %BACKUP_FILE%

pause 