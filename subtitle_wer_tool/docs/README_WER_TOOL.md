# 字幕WER计算工具

一个专业的字幕质量评估工具，支持从视频中提取字幕并计算词错误率(WER)。

## 🚀 快速开始

### 一键启动
```bash
# Windows用户
start_subtitle_wer_gui.bat

# Linux/macOS用户
python start_subtitle_wer_gui.py
```

### 安装依赖
```bash
# 基础依赖
pip install -r docs/wer_tool/requirements_gui.txt

# 安装PaddleOCR（推荐，中文识别效果最佳）
python tools/installers/install_paddleocr.py

# 安装高级字幕提取工具（可选）
python tools/installers/install_advanced_tools.py
```

## 📁 目录结构

```
stream_recognizer/
├── start_subtitle_wer_gui.py          # 主启动脚本
├── start_subtitle_wer_gui.bat         # Windows启动脚本
├── README_WER_TOOL.md                 # 本文件
│
├── tools/                             # 工具模块
│   ├── subtitle_wer/                  # 字幕WER核心功能
│   │   ├── subtitle_wer_gui.py        # GUI主程序
│   │   ├── subtitle_wer_tool.py       # 命令行工具
│   │   └── advanced_subtitle_extractor.py  # 高级字幕提取器
│   │
│   ├── ocr/                           # OCR识别模块
│   │   ├── improved_ocr_extractor.py  # 改进的OCR提取器（推荐）
│   │   ├── video_ocr_extractor.py     # 原版OCR提取器
│   │   └── advanced_ocr_extractor.py  # 高级OCR提取器
│   │
│   ├── installers/                    # 安装脚本
│   │   ├── install_paddleocr.py       # PaddleOCR安装器
│   │   ├── install_paddleocr.bat      # Windows PaddleOCR安装器
│   │   └── install_advanced_tools.py  # 高级工具安装器
│   │
│   └── tests/                         # 测试脚本
│       ├── test_improved_ocr.py       # OCR功能测试
│       └── test_subtitle_wer_tool.py  # WER工具测试
│
├── scripts/                           # 启动脚本
│   ├── run_gui.bat                    # GUI启动脚本(Windows)
│   └── run_gui.sh                     # GUI启动脚本(Linux/macOS)
│
├── docs/wer_tool/                     # 文档
│   ├── README_GUI.md                  # GUI详细文档
│   ├── README_subtitle_wer_tool.md    # 命令行工具文档
│   └── requirements_gui.txt           # 依赖列表
│
└── [其他stream_recognizer项目文件...]
```

## 🎯 主要功能

### 1. 多种字幕提取方式
- **内嵌字幕提取**: 支持FFmpeg、MKVToolNix、MediaInfo等工具
- **OCR硬字幕识别**: 支持PaddleOCR、EasyOCR、Tesseract等引擎
- **智能回退机制**: 自动选择最佳提取方案

### 2. 先进的OCR技术
- **PaddleOCR**: 中文识别效果最佳，强烈推荐
- **EasyOCR**: 多语言支持，识别速度快
- **Tesseract**: 开源免费，基础功能
- **GPU加速**: 支持CUDA加速，提升2-5倍处理速度

### 3. 专业的WER计算
- **精确算法**: 基于编辑距离的标准WER计算
- **详细统计**: 提供替换、删除、插入等详细错误分析
- **可视化结果**: 颜色编码显示识别质量等级

### 4. 友好的用户界面
- **媒体播放器**: 内置视频播放功能
- **实时预览**: 边播放边查看字幕效果
- **批量处理**: 支持多个字幕流的处理
- **结果导出**: 支持结果保存和分享

## 🛠️ 使用说明

### GUI版本（推荐）
1. 运行 `start_subtitle_wer_gui.py` 或 `start_subtitle_wer_gui.bat`
2. 选择视频文件
3. 配置OCR设置（推荐使用PaddleOCR）
4. 点击"提取字幕"
5. 输入识别结果文本
6. 计算WER并查看结果

### 命令行版本
```bash
# 基础使用
python tools/subtitle_wer/subtitle_wer_tool.py video.mp4 reference.srt hypothesis.txt

# 查看详细帮助
python tools/subtitle_wer/subtitle_wer_tool.py --help
```

## 📊 OCR引擎对比

| 引擎 | 中文效果 | 英文效果 | 速度 | GPU支持 | 推荐度 |
|------|----------|----------|------|---------|---------|
| PaddleOCR | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐ | ⭐⭐⭐⭐ | ✅ | 🌟🌟🌟🌟🌟 |
| EasyOCR | ⭐⭐⭐ | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ | ✅ | 🌟🌟🌟🌟 |
| Tesseract | ⭐⭐ | ⭐⭐⭐ | ⭐⭐⭐ | ❌ | 🌟🌟 |

## 🔧 故障排除

### 常见问题

**Q: 程序无法启动**
```bash
# 检查Python版本（需要3.6+）
python --version

# 安装依赖
pip install -r docs/wer_tool/requirements_gui.txt
```

**Q: OCR识别效果不好**
```bash
# 安装PaddleOCR
python tools/installers/install_paddleocr.py

# 在GUI中选择"PaddleOCR (推荐)"
```

**Q: GPU加速不工作**
```bash
# 检查CUDA支持
python -c "import torch; print('CUDA可用:', torch.cuda.is_available())"

# 安装GPU版本
pip install paddlepaddle-gpu
```

### 测试功能
```bash
# 测试OCR功能
python tools/tests/test_improved_ocr.py

# 测试WER计算
python tools/tests/test_subtitle_wer_tool.py
```

## 🤝 技术支持

如果遇到问题：
1. 查看 `docs/wer_tool/README_GUI.md` 获取详细文档
2. 运行测试脚本检查功能状态
3. 检查依赖安装是否完整

## 📝 更新日志

### v2.0 (当前版本)
- ✅ 集成PaddleOCR，大幅提升中文识别效果
- ✅ 重构目录结构，提高代码组织性
- ✅ 增加GPU加速支持
- ✅ 优化用户界面和体验
- ✅ 添加自动安装脚本

### v1.0
- ✅ 基础字幕提取和WER计算功能
- ✅ GUI界面
- ✅ EasyOCR和Tesseract支持

---

🎉 **享受更准确的字幕质量评估！** 