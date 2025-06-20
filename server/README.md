 # Whisper LoRA 微调系统

本目录包含使用LoRA技术微调Whisper模型的完整解决方案。

## 🚀 功能特性

- **高效微调**: 使用LoRA技术，仅训练少量参数，大幅降低计算成本
- **多语言支持**: 支持中文、英文等多种语言的微调
- **灵活配置**: 支持多种Whisper模型规格（tiny、base、small、medium、large）
- **批量推理**: 支持单文件和批量音频文件转录
- **时间戳输出**: 可选择输出带时间戳的转录结果
- **长音频处理**: 自动分块处理长音频文件

## 📋 环境要求

- Python 3.8+
- PyTorch 2.0+
- CUDA 11.8+ (如果使用GPU)
- 至少8GB显存 (推荐16GB+)

## 🛠️ 环境配置

### Windows系统

```bash
# 运行环境配置脚本
setup_environment.bat
```

### Linux/macOS系统

```bash
# 给脚本执行权限
chmod +x setup_environment.sh

# 运行环境配置脚本
./setup_environment.sh
```

### 手动配置

```bash
# 创建虚拟环境
python -m venv whisper_lora_env

# 激活虚拟环境 (Windows)
whisper_lora_env\Scripts\activate

# 激活虚拟环境 (Linux/macOS)
source whisper_lora_env/bin/activate

# 安装依赖
pip install -r requirements.txt
```

## 📊 数据准备

### 数据格式

训练数据需要准备为JSON格式，参考 `data_format_example.json`：

```json
[
  {
    "audio_path": "./audio/sample1.wav",
    "text": "这是第一个音频文件的转录文本。"
  },
  {
    "audio_path": "./audio/sample2.wav", 
    "text": "这是第二个音频文件的转录文本。"
  }
]
```

### 音频要求

- **格式**: 支持 WAV, MP3, M4A, FLAC 等常见格式
- **采样率**: 建议16kHz（会自动转换）
- **时长**: 建议1-30秒每段，最长不超过30秒
- **质量**: 清晰的语音，较少背景噪音

### 数据准备步骤

1. **收集音频文件**: 将音频文件放在统一目录下
2. **准备转录文本**: 确保转录文本准确无误
3. **创建JSON文件**: 按照上述格式创建训练和验证数据文件
4. **数据验证**: 确保所有音频文件路径正确，文本编码为UTF-8

## 🏋️ 模型训练

### 基础训练命令

```bash
python whisper_lora_finetune.py \
    --model_name_or_path openai/whisper-small \
    --train_file data/train.json \
    --validation_file data/validation.json \
    --output_dir ./output \
    --num_train_epochs 3 \
    --per_device_train_batch_size 4 \
    --gradient_accumulation_steps 2 \
    --learning_rate 5e-4 \
    --warmup_steps 500 \
    --logging_steps 50 \
    --save_steps 500 \
    --eval_steps 500 \
    --fp16 \
    --gradient_checkpointing
```

### 高级训练选项

```bash
python whisper_lora_finetune.py \
    --model_name_or_path openai/whisper-medium \
    --train_file data/train.json \
    --validation_file data/validation.json \
    --output_dir ./output \
    --num_train_epochs 5 \
    --per_device_train_batch_size 2 \
    --gradient_accumulation_steps 4 \
    --learning_rate 1e-4 \
    --warmup_steps 1000 \
    --lora_r 16 \
    --lora_alpha 64 \
    --lora_dropout 0.05 \
    --max_train_samples 10000 \
    --max_eval_samples 1000 \
    --use_wandb \
    --wandb_project my-whisper-project
```

### 参数说明

#### 模型参数
- `--model_name_or_path`: 基础模型路径（whisper-tiny/base/small/medium/large）
- `--cache_dir`: 模型缓存目录

#### 数据参数
- `--train_file`: 训练数据文件路径
- `--validation_file`: 验证数据文件路径
- `--max_train_samples`: 最大训练样本数
- `--max_eval_samples`: 最大验证样本数

#### LoRA参数
- `--lora_r`: LoRA rank，控制参数量（4-64，越大参数越多）
- `--lora_alpha`: LoRA alpha，通常设为rank的2-4倍
- `--lora_dropout`: LoRA dropout率（0.05-0.1）

#### 训练参数
- `--num_train_epochs`: 训练轮数
- `--per_device_train_batch_size`: 每设备批次大小
- `--gradient_accumulation_steps`: 梯度累积步数
- `--learning_rate`: 学习率
- `--warmup_steps`: 预热步数
- `--fp16`: 使用半精度训练
- `--gradient_checkpointing`: 梯度检查点（节省显存）

## 🔍 模型推理

### 单文件转录

```bash
python whisper_lora_inference.py \
    --model_path ./output \
    --audio_path audio/test.wav \
    --language zh \
    --return_timestamps
```

### 批量转录

```bash
# 创建音频文件列表
echo "audio/file1.wav" > audio_list.txt
echo "audio/file2.wav" >> audio_list.txt

# 批量转录
python whisper_lora_inference.py \
    --model_path ./output \
    --audio_list audio_list.txt \
    --output_file results.json \
    --language zh
```

### 推理参数说明

- `--model_path`: 训练好的模型路径
- `--audio_path`: 单个音频文件路径
- `--audio_list`: 音频文件列表
- `--output_file`: 输出结果文件
- `--language`: 语言代码（zh、en、ja等）
- `--task`: 任务类型（transcribe或translate）
- `--return_timestamps`: 返回时间戳
- `--device`: 计算设备（auto、cuda、cpu）

## 📈 监控和调试

### 使用Weights & Biases

```bash
pip install wandb
wandb login

python whisper_lora_finetune.py \
    --use_wandb \
    --wandb_project whisper-lora-project \
    [其他参数...]
```

### 使用TensorBoard

```bash
pip install tensorboard
tensorboard --logdir ./output/runs
```

## 🎯 性能优化建议

### 内存优化

1. **减小批次大小**: 降低 `per_device_train_batch_size`
2. **增加梯度累积**: 提高 `gradient_accumulation_steps`
3. **启用梯度检查点**: 使用 `--gradient_checkpointing`
4. **使用FP16**: 启用 `--fp16`

### 训练速度优化

1. **使用更大的批次**: 在显存允许的情况下增大批次
2. **减少评估频率**: 增大 `eval_steps` 和 `save_steps`
3. **使用多GPU**: 配置分布式训练
4. **选择合适的模型**: 从小模型开始试验

### LoRA参数调优

| 参数 | 建议值 | 说明 |
|------|--------|------|
| lora_r | 8-16 | 平衡性能和参数量 |
| lora_alpha | 16-32 | 通常为r的2倍 |
| lora_dropout | 0.05-0.1 | 防止过拟合 |

## 🐛 常见问题

### 内存不足

```
RuntimeError: CUDA out of memory
```

**解决方案**:
- 减小批次大小至1或2
- 启用梯度检查点
- 使用FP16训练
- 选择更小的模型

### 数据加载错误

```
FileNotFoundError: [Errno 2] No such file or directory
```

**解决方案**:
- 检查音频文件路径是否正确
- 确保JSON文件格式正确
- 验证文件编码为UTF-8

### 模型加载失败

```
OSError: Can't load weights
```

**解决方案**:
- 检查模型路径是否正确
- 确保网络连接正常（首次下载模型）
- 清除缓存目录重新下载

## 📝 许可证

本项目基于MIT许可证开源。

## 🤝 贡献

欢迎提交Issue和Pull Request来改进本项目！

## 📞 支持

如有问题，请在GitHub上提交Issue或联系开发团队。