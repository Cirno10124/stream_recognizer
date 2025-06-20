 #!/bin/bash

# Whisper LoRA微调环境配置脚本
# 适用于Linux/macOS系统

echo "开始配置Whisper LoRA微调环境..."

# 检查Python版本
python_version=$(python3 --version 2>&1 | grep -Po '(?<=Python )(.+)')
required_version="3.8"

if [[ "$(printf '%s\n' "$required_version" "$python_version" | sort -V | head -n1)" != "$required_version" ]]; then
    echo "错误: 需要Python 3.8或更高版本，当前版本: $python_version"
    exit 1
fi

echo "Python版本检查通过: $python_version"

# 创建虚拟环境
echo "创建虚拟环境..."
python3 -m venv whisper_lora_env

# 激活虚拟环境
echo "激活虚拟环境..."
source whisper_lora_env/bin/activate

# 升级pip
echo "升级pip..."
pip install --upgrade pip

# 安装PyTorch (根据系统选择合适的版本)
echo "安装PyTorch..."
if command -v nvidia-smi &> /dev/null; then
    echo "检测到NVIDIA GPU，安装CUDA版本的PyTorch..."
    # 检查CUDA版本
    cuda_version=$(nvidia-smi | grep -Po 'CUDA Version: \K[0-9.]+')
    echo "检测到CUDA版本: $cuda_version"
    
    if [[ "$cuda_version" == "12."* ]]; then
        pip install torch torchvision torchaudio --index-url https://download.pytorch.org/whl/cu121
    elif [[ "$cuda_version" == "11.8"* ]]; then
        pip install torch torchvision torchaudio --index-url https://download.pytorch.org/whl/cu118
    else
        echo "警告: 未识别的CUDA版本，安装默认PyTorch版本"
        pip install torch torchvision torchaudio
    fi
else
    echo "未检测到NVIDIA GPU，安装CPU版本的PyTorch..."
    pip install torch torchvision torchaudio --index-url https://download.pytorch.org/whl/cpu
fi

# 安装其他依赖
echo "安装其他依赖包..."
pip install -r requirements.txt

# 安装ffmpeg (音频处理需要)
echo "检查并安装ffmpeg..."
if ! command -v ffmpeg &> /dev/null; then
    echo "ffmpeg未安装，正在安装..."
    if [[ "$OSTYPE" == "linux-gnu"* ]]; then
        # Linux
        sudo apt-get update
        sudo apt-get install -y ffmpeg
    elif [[ "$OSTYPE" == "darwin"* ]]; then
        # macOS
        if command -v brew &> /dev/null; then
            brew install ffmpeg
        else
            echo "请先安装Homebrew，然后运行: brew install ffmpeg"
        fi
    fi
else
    echo "ffmpeg已安装"
fi

# 验证安装
echo "验证安装..."
python3 -c "
import torch
import transformers
import peft
import librosa
import datasets
print('PyTorch版本:', torch.__version__)
print('Transformers版本:', transformers.__version__)
print('PEFT版本:', peft.__version__)
print('Librosa版本:', librosa.__version__)
print('Datasets版本:', datasets.__version__)
print('CUDA可用:', torch.cuda.is_available())
if torch.cuda.is_available():
    print('GPU数量:', torch.cuda.device_count())
    print('GPU名称:', torch.cuda.get_device_name(0))
print('环境配置成功!')
"

# 创建示例配置文件
echo "创建示例配置文件..."
cat > config_example.yaml << 'EOF'
# Whisper LoRA微调配置示例

# 模型配置
model:
  name_or_path: "openai/whisper-small"  # 可选: whisper-tiny, whisper-base, whisper-small, whisper-medium, whisper-large
  cache_dir: "./model_cache"
  
# 数据配置
data:
  train_file: "./data/train.json"
  validation_file: "./data/validation.json"
  max_train_samples: null  # 设置为数字限制样本数量
  max_eval_samples: null
  max_duration: 30.0  # 最大音频长度（秒）
  min_duration: 1.0   # 最小音频长度（秒）

# LoRA配置
lora:
  use_lora: true
  r: 8              # LoRA rank，越大参数越多
  alpha: 32         # LoRA alpha，通常设为r的2-4倍
  dropout: 0.1      # LoRA dropout
  target_modules:   # 目标模块
    - "q_proj"
    - "k_proj"
    - "v_proj"
    - "out_proj"
    - "fc1"
    - "fc2"

# 训练配置
training:
  output_dir: "./output"
  num_train_epochs: 3
  per_device_train_batch_size: 4
  per_device_eval_batch_size: 4
  gradient_accumulation_steps: 2
  learning_rate: 5e-4
  warmup_steps: 500
  logging_steps: 50
  save_steps: 500
  eval_steps: 500
  fp16: true
  gradient_checkpointing: true
  dataloader_num_workers: 4

# 监控配置
monitoring:
  use_wandb: false
  wandb_project: "whisper-lora"
  
# 其他配置
misc:
  seed: 42
EOF

echo "环境配置完成!"
echo ""
echo "使用说明:"
echo "1. 激活虚拟环境: source whisper_lora_env/bin/activate"
echo "2. 查看示例配置: cat config_example.yaml"
echo "3. 准备数据格式请参考data_format_example.json"
echo "4. 运行微调: python whisper_lora_finetune.py --train_file data/train.json --output_dir output"
echo ""
echo "如果遇到问题，请检查:"
echo "- Python版本是否>=3.8"
echo "- 是否有足够的GPU内存"
echo "- 数据文件格式是否正确"