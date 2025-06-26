# 字幕WER计算工具

## 📁 项目结构

```
subtitle_wer_tool/
├── README.md                   # 本文件 - 项目总览
├── start_subtitle_wer_gui.py   # 主启动脚本
├── start_subtitle_wer_gui.bat  # Windows启动脚本
├── subtitle_wer_gui.py         # GUI主程序
├── docs/                       # 文档目录
│   └── README_WER_TOOL.md     # 详细使用说明
├── scripts/                    # 脚本目录
│   ├── run_gui.bat            # Windows GUI启动脚本
│   └── run_gui.sh             # Linux/macOS GUI启动脚本
└── tools/                      # 工具模块目录
    ├── ocr/                   # OCR识别模块
    ├── subtitle_wer/          # 字幕WER计算模块
    ├── tests/                 # 测试文件
    └── installers/            # 安装脚本
```

## 🚀 快速开始

### 启动GUI工具
```bash
# Windows
python start_subtitle_wer_gui.py

# 或双击
start_subtitle_wer_gui.bat
```

### 主要功能
1. **视频字幕提取** - 支持内嵌字幕和OCR硬字幕识别
2. **WER计算** - 计算Whisper识别结果与字幕的词错误率
3. **媒体播放器** - 内置视频播放器，支持播放控制
4. **结果分析** - 详细的错误分析和可视化结果

## 📖 详细文档

请查看 `docs/README_WER_TOOL.md` 获取详细的使用说明和技术文档。

## 🔧 安装依赖

### 自动安装
```bash
python tools/installers/install_paddleocr.py
```

### 手动安装
```bash
pip install PyQt5 opencv-python easyocr paddleocr tesseract
```

## 📋 系统要求

- Python 3.8+
- PyQt5
- OpenCV
- EasyOCR / PaddleOCR / Tesseract
- FFmpeg（用于视频处理）

## 🎯 特色功能

- **多引擎OCR支持** - EasyOCR、PaddleOCR、Tesseract
- **GPU加速** - 支持CUDA加速的OCR识别
- **智能字幕提取** - 先尝试内嵌字幕，失败时自动启用OCR
- **多语言支持** - 中文、英文、日文、韩文等
- **完整的GUI界面** - 用户友好的图形界面

## 🔍 使用场景

1. **语音识别评估** - 评估Whisper等语音识别系统的准确性
2. **字幕质量检查** - 检查视频字幕的准确性
3. **多模态数据分析** - 结合视频、音频、文本的综合分析
4. **教学评估** - 语言学习和教学效果评估

## 🤝 贡献

欢迎提交Issues和Pull Requests来改进这个工具！

## 📄 许可证

本项目采用MIT许可证。 