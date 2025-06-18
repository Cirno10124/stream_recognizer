# RNNoise手动编译指南（解决wget问题）

## 问题说明

在Windows环境下使用Git Bash编译RNNoise时，`autogen.sh`脚本中的`wget`命令可能无法正常工作，即使安装了wget也可能出现路径或环境变量问题。

## 解决方案

### 方案1：使用PowerShell脚本（推荐）

我已经为您创建了`build_rnnoise_windows.ps1`脚本，直接运行即可：

```powershell
# 在PowerShell中运行
.\build_rnnoise_windows.ps1
```

### 方案2：手动步骤

如果脚本不工作，请按以下步骤手动操作：

#### 步骤1：下载RNNoise源码

```bash
git clone https://github.com/xiph/rnnoise.git
cd rnnoise
```

#### 步骤2：手动下载模型文件

由于wget问题，我们手动下载模型：

**使用浏览器下载：**
1. 打开 https://media.xiph.org/rnnoise/models/rnnoise_data-10052018.tar.gz
2. 下载文件到rnnoise目录

**或使用curl：**
```bash
curl -L -o rnnoise_data-10052018.tar.gz https://media.xiph.org/rnnoise/models/rnnoise_data-10052018.tar.gz
```

**或使用PowerShell：**
```powershell
Invoke-WebRequest -Uri "https://media.xiph.org/rnnoise/models/rnnoise_data-10052018.tar.gz" -OutFile "rnnoise_data-10052018.tar.gz"
```

#### 步骤3：解压模型文件

```bash
# 如果有tar命令
tar -xzf rnnoise_data-10052018.tar.gz

# 如果没有tar，使用7-Zip或WinRAR手动解压
```

#### 步骤4：修改autogen.sh

编辑`autogen.sh`文件，将所有的`wget`替换为`curl -L -O`：

```bash
# 原来的
wget https://media.xiph.org/rnnoise/models/rnnoise_data-10052018.tar.gz

# 改为
curl -L -O https://media.xiph.org/rnnoise/models/rnnoise_data-10052018.tar.gz
```

#### 步骤5：编译

根据您的环境选择：

**MSYS2环境：**
```bash
./autogen.sh
./configure
make
```

**MinGW环境：**
```bash
bash autogen.sh
bash configure
make
```

**Visual Studio环境：**
使用我创建的vcxproj文件或手动编译。

### 方案3：使用预编译库

如果编译仍有问题，可以使用预编译的库：

#### Windows预编译库下载

1. **从vcpkg安装：**
```cmd
vcpkg install rnnoise
```

2. **从conda安装：**
```cmd
conda install -c conda-forge rnnoise
```

3. **手动下载预编译库：**
   - 访问 https://github.com/xiph/rnnoise/releases
   - 下载Windows版本的预编译库

### 方案4：简化版本（无需模型文件）

如果只是想快速测试，可以创建一个简化版本：

#### 创建简化的rnnoise.h

```c
// 简化版rnnoise.h
#ifndef RNNOISE_H
#define RNNOISE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DenoiseState DenoiseState;

DenoiseState *rnnoise_create(void *model);
void rnnoise_destroy(DenoiseState *st);
float rnnoise_process_frame(DenoiseState *st, float *out, const float *in);

#ifdef __cplusplus
}
#endif

#endif
```

#### 创建简化的实现

```c
// 简化版实现（仅用于编译测试）
#include "rnnoise.h"
#include <stdlib.h>
#include <string.h>

struct DenoiseState {
    int dummy;
};

DenoiseState *rnnoise_create(void *model) {
    return (DenoiseState*)malloc(sizeof(DenoiseState));
}

void rnnoise_destroy(DenoiseState *st) {
    if (st) free(st);
}

float rnnoise_process_frame(DenoiseState *st, float *out, const float *in) {
    // 简单的直通处理（无实际降噪）
    memcpy(out, in, 480 * sizeof(float));
    return 0.0f; // 返回VAD概率
}
```

## 集成到项目

### 1. 复制文件

将编译好的文件复制到项目目录：

```
stream_recognizer/
├── lib/
│   └── rnnoise.lib      # 静态库
├── include/
│   └── rnnoise.h        # 头文件
```

### 2. 修改项目配置

在Visual Studio项目中添加：

```xml
<ItemDefinitionGroup>
  <ClCompile>
    <PreprocessorDefinitions>RNNOISE_AVAILABLE;%(PreprocessorDefinitions)</PreprocessorDefinitions>
  </ClCompile>
  <Link>
    <AdditionalDependencies>rnnoise.lib;%(AdditionalDependencies)</AdditionalDependencies>
  </Link>
</ItemDefinitionGroup>
```

### 3. 测试集成

编译项目并测试噪声抑制功能：

```cpp
#include "audio_preprocessor.h"

AudioPreprocessor preprocessor;
if (preprocessor.isNoiseSuppressionAvailable()) {
    std::cout << "RNNoise集成成功！" << std::endl;
} else {
    std::cout << "RNNoise不可用" << std::endl;
}
```

## 常见问题解决

### 1. wget命令找不到

**原因：** Git Bash中wget不在PATH中
**解决：** 使用curl替代或手动下载

### 2. 编译环境问题

**原因：** 缺少必要的编译工具
**解决：** 安装MSYS2、MinGW或Visual Studio

### 3. 模型文件下载失败

**原因：** 网络问题或防火墙
**解决：** 使用VPN或手动下载

### 4. 库文件链接错误

**原因：** 路径配置错误
**解决：** 检查库文件路径和架构匹配

## 验证安装

运行以下命令验证安装：

```bash
# 检查库文件
ls -la lib/rnnoise.lib

# 检查头文件
ls -la include/rnnoise.h

# 编译测试
g++ -DRNNOISE_AVAILABLE test.cpp -lrnnoise
```

如果一切正常，您应该能够在项目中使用RNNoise噪声抑制功能了！ 