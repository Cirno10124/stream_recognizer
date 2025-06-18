# RNNoise Windows编译脚本
# 解决wget问题并自动化编译过程

Write-Host "=== RNNoise Windows编译脚本 ===" -ForegroundColor Green

# 检查必要工具
Write-Host "1. 检查必要工具..." -ForegroundColor Yellow

# 检查Git
try {
    $gitVersion = git --version
    Write-Host "✓ Git已安装: $gitVersion" -ForegroundColor Green
} catch {
    Write-Host "✗ Git未安装，请先安装Git" -ForegroundColor Red
    exit 1
}

# 检查curl
try {
    $curlVersion = curl --version | Select-Object -First 1
    Write-Host "✓ Curl已安装: $curlVersion" -ForegroundColor Green
} catch {
    Write-Host "✗ Curl未安装" -ForegroundColor Red
}

# 创建工作目录
$workDir = "rnnoise_build"
if (Test-Path $workDir) {
    Write-Host "清理现有目录..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force $workDir
}

New-Item -ItemType Directory -Path $workDir | Out-Null
Set-Location $workDir

Write-Host "2. 克隆RNNoise仓库..." -ForegroundColor Yellow
git clone https://github.com/xiph/rnnoise.git
if ($LASTEXITCODE -ne 0) {
    Write-Host "✗ 克隆失败" -ForegroundColor Red
    exit 1
}

Set-Location rnnoise

Write-Host "3. 手动下载模型文件..." -ForegroundColor Yellow
# 手动下载模型文件，避免autogen.sh中的wget问题
$modelUrl = "https://media.xiph.org/rnnoise/models/rnnoise_data-10052018.tar.gz"
$modelFile = "rnnoise_data-10052018.tar.gz"

try {
    Invoke-WebRequest -Uri $modelUrl -OutFile $modelFile -UseBasicParsing
    Write-Host "✓ 模型文件下载成功" -ForegroundColor Green
} catch {
    Write-Host "✗ 模型文件下载失败，尝试使用curl..." -ForegroundColor Yellow
    curl -L -o $modelFile $modelUrl
    if ($LASTEXITCODE -ne 0) {
        Write-Host "✗ curl下载也失败" -ForegroundColor Red
        exit 1
    }
}

# 解压模型文件
Write-Host "4. 解压模型文件..." -ForegroundColor Yellow
if (Get-Command tar -ErrorAction SilentlyContinue) {
    tar -xzf $modelFile
    Write-Host "✓ 使用tar解压成功" -ForegroundColor Green
} else {
    # 如果没有tar，使用7-Zip或其他方法
    Write-Host "⚠ 系统没有tar命令，请手动解压 $modelFile" -ForegroundColor Yellow
    Write-Host "  可以使用7-Zip或WinRAR解压" -ForegroundColor Yellow
}

# 检查是否有MSYS2/MinGW环境
Write-Host "5. 检查编译环境..." -ForegroundColor Yellow

$hasMSYS2 = Test-Path "C:\msys64\usr\bin\bash.exe"
$hasMinGW = Test-Path "C:\MinGW\bin\gcc.exe"
$hasVS = Get-Command cl -ErrorAction SilentlyContinue

if ($hasMSYS2) {
    Write-Host "✓ 检测到MSYS2环境" -ForegroundColor Green
    Write-Host "使用MSYS2编译..." -ForegroundColor Yellow
    
    # 修改autogen.sh，替换wget为curl
    $autogenContent = Get-Content "autogen.sh" -Raw
    $autogenContent = $autogenContent -replace 'wget', 'curl -L -O'
    Set-Content "autogen.sh" $autogenContent
    
    # 在MSYS2环境中编译
    C:\msys64\usr\bin\bash.exe -c "cd $(pwd -W) && ./autogen.sh && ./configure && make"
    
} elseif ($hasMinGW) {
    Write-Host "✓ 检测到MinGW环境" -ForegroundColor Green
    Write-Host "使用MinGW编译..." -ForegroundColor Yellow
    
    # 设置MinGW环境
    $env:PATH = "C:\MinGW\bin;$env:PATH"
    
    # 修改autogen.sh
    $autogenContent = Get-Content "autogen.sh" -Raw
    $autogenContent = $autogenContent -replace 'wget', 'curl -L -O'
    Set-Content "autogen.sh" $autogenContent
    
    bash autogen.sh
    bash configure
    make
    
} elseif ($hasVS) {
    Write-Host "✓ 检测到Visual Studio环境" -ForegroundColor Green
    Write-Host "需要手动使用Visual Studio编译" -ForegroundColor Yellow
    
    # 创建简化的Visual Studio项目文件
    $vcxprojContent = @"
<?xml version="1.0" encoding="utf-8"?>
<Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup Label="ProjectConfigurations">
    <ProjectConfiguration Include="Release|x64">
      <Configuration>Release</Configuration>
      <Platform>x64</Platform>
    </ProjectConfiguration>
  </ItemGroup>
  <PropertyGroup Label="Globals">
    <ProjectGuid>{12345678-1234-1234-1234-123456789012}</ProjectGuid>
    <RootNamespace>rnnoise</RootNamespace>
    <WindowsTargetPlatformVersion>10.0</WindowsTargetPlatformVersion>
  </PropertyGroup>
  <Import Project="`$(VCTargetsPath)\Microsoft.Cpp.Default.props" />
  <PropertyGroup Condition="'`$(Configuration)|`$(Platform)'=='Release|x64'" Label="Configuration">
    <ConfigurationType>StaticLibrary</ConfigurationType>
    <UseDebugLibraries>false</UseDebugLibraries>
    <PlatformToolset>v143</PlatformToolset>
    <WholeProgramOptimization>true</WholeProgramOptimization>
    <CharacterSet>Unicode</CharacterSet>
  </PropertyGroup>
  <Import Project="`$(VCTargetsPath)\Microsoft.Cpp.props" />
  <PropertyGroup Condition="'`$(Configuration)|`$(Platform)'=='Release|x64'">
    <OutDir>lib\</OutDir>
    <IntDir>obj\</IntDir>
    <TargetName>rnnoise</TargetName>
  </PropertyGroup>
  <ItemDefinitionGroup Condition="'`$(Configuration)|`$(Platform)'=='Release|x64'">
    <ClCompile>
      <WarningLevel>Level3</WarningLevel>
      <Optimization>MaxSpeed</Optimization>
      <FunctionLevelLinking>true</FunctionLevelLinking>
      <IntrinsicFunctions>true</IntrinsicFunctions>
      <PreprocessorDefinitions>WIN32;NDEBUG;_LIB;%(PreprocessorDefinitions)</PreprocessorDefinitions>
      <AdditionalIncludeDirectories>include;src</AdditionalIncludeDirectories>
    </ClCompile>
    <Lib>
      <AdditionalDependencies></AdditionalDependencies>
    </Lib>
  </ItemDefinitionGroup>
  <ItemGroup>
    <ClCompile Include="src\denoise.c" />
    <ClCompile Include="src\rnn.c" />
    <ClCompile Include="src\rnn_data.c" />
    <ClCompile Include="src\pitch.c" />
    <ClCompile Include="src\kiss_fft.c" />
  </ItemGroup>
  <ItemGroup>
    <ClInclude Include="include\rnnoise.h" />
  </ItemGroup>
  <Import Project="`$(VCTargetsPath)\Microsoft.Cpp.targets" />
</Project>
"@
    
    Set-Content "rnnoise.vcxproj" $vcxprojContent
    Write-Host "✓ 已创建Visual Studio项目文件: rnnoise.vcxproj" -ForegroundColor Green
    
} else {
    Write-Host "✗ 未检测到合适的编译环境" -ForegroundColor Red
    Write-Host "请安装以下之一:" -ForegroundColor Yellow
    Write-Host "  - MSYS2: https://www.msys2.org/" -ForegroundColor Yellow
    Write-Host "  - MinGW: https://www.mingw-w64.org/" -ForegroundColor Yellow
    Write-Host "  - Visual Studio: https://visualstudio.microsoft.com/" -ForegroundColor Yellow
}

Write-Host "6. 检查编译结果..." -ForegroundColor Yellow

if (Test-Path ".libs\librnnoise.a") {
    Write-Host "✓ 静态库编译成功: .libs\librnnoise.a" -ForegroundColor Green
    
    # 复制文件到项目目录
    $projectDir = Split-Path -Parent (Split-Path -Parent (Get-Location))
    Copy-Item ".libs\librnnoise.a" "$projectDir\lib\rnnoise.lib" -Force
    Copy-Item "include\rnnoise.h" "$projectDir\include\" -Force
    
    Write-Host "✓ 文件已复制到项目目录" -ForegroundColor Green
    
} elseif (Test-Path "lib\rnnoise.lib") {
    Write-Host "✓ 静态库编译成功: lib\rnnoise.lib" -ForegroundColor Green
    
    # 复制文件到项目目录
    $projectDir = Split-Path -Parent (Split-Path -Parent (Get-Location))
    Copy-Item "lib\rnnoise.lib" "$projectDir\lib\" -Force
    Copy-Item "include\rnnoise.h" "$projectDir\include\" -Force
    
    Write-Host "✓ 文件已复制到项目目录" -ForegroundColor Green
    
} else {
    Write-Host "⚠ 未找到编译结果，可能需要手动编译" -ForegroundColor Yellow
}

Write-Host "=== 编译完成 ===" -ForegroundColor Green
Write-Host "如果编译成功，请在项目中定义 RNNOISE_AVAILABLE 宏" -ForegroundColor Yellow

# 返回原目录
Set-Location ..\..

Write-Host "完整路径信息:" -ForegroundColor Cyan
Write-Host "当前目录: $(Get-Location)" -ForegroundColor Cyan
Write-Host "RNNoise源码: $(Get-Location)\$workDir\rnnoise" -ForegroundColor Cyan 