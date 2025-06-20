#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Whisper模型LoRA微调脚本
支持使用LoRA技术对Whisper模型进行高效微调
"""

import os
import json
import torch
import argparse
import logging
from typing import Dict, List, Optional, Tuple
from dataclasses import dataclass, field
from pathlib import Path
import numpy as np
from tqdm import tqdm

# Transformers相关导入
from transformers import (
    WhisperProcessor, 
    WhisperForConditionalGeneration,
    WhisperConfig,
    Seq2SeqTrainer,
    Seq2SeqTrainingArguments,
    EarlyStoppingCallback,
    TrainerCallback
)

# PEFT相关导入
from peft import (
    LoraConfig,
    get_peft_model,
    TaskType,
    PeftModel,
    PeftConfig
)

# 数据集相关导入
from datasets import Dataset, DatasetDict, Audio
import librosa

# 设置日志
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)

@dataclass
class ModelArguments:
    """模型相关参数"""
    model_name_or_path: str = field(
        default="openai/whisper-small",
        metadata={"help": "Whisper模型路径或名称"}
    )
    cache_dir: Optional[str] = field(
        default=None,
        metadata={"help": "模型缓存目录"}
    )

@dataclass
class DataArguments:
    """数据相关参数"""
    train_file: Optional[str] = field(
        default=None,
        metadata={"help": "训练数据文件路径"}
    )
    validation_file: Optional[str] = field(
        default=None,
        metadata={"help": "验证数据文件路径"}
    )
    max_train_samples: Optional[int] = field(
        default=None,
        metadata={"help": "最大训练样本数"}
    )
    max_eval_samples: Optional[int] = field(
        default=None,
        metadata={"help": "最大评估样本数"}
    )
    audio_column_name: str = field(
        default="audio",
        metadata={"help": "音频列名称"}
    )
    text_column_name: str = field(
        default="sentence",
        metadata={"help": "文本列名称"}
    )
    max_duration_in_seconds: float = field(
        default=30.0,
        metadata={"help": "最大音频长度（秒）"}
    )

@dataclass
class LoraArguments:
    """LoRA相关参数"""
    use_lora: bool = field(
        default=True,
        metadata={"help": "是否使用LoRA"}
    )
    lora_r: int = field(
        default=8,
        metadata={"help": "LoRA rank"}
    )
    lora_alpha: int = field(
        default=32,
        metadata={"help": "LoRA alpha"}
    )
    lora_dropout: float = field(
        default=0.1,
        metadata={"help": "LoRA dropout"}
    )

class WhisperLoRATrainer:
    """Whisper LoRA微调训练器"""
    
    def __init__(
        self,
        model_args: ModelArguments,
        data_args: DataArguments,
        lora_args: LoraArguments,
        training_args: Seq2SeqTrainingArguments
    ):
        self.model_args = model_args
        self.data_args = data_args
        self.lora_args = lora_args
        self.training_args = training_args
        
        # 初始化处理器
        self.processor = WhisperProcessor.from_pretrained(
            model_args.model_name_or_path,
            cache_dir=model_args.cache_dir
        )
        
        # 初始化模型
        self.model = self._load_model()
        
        # 应用LoRA
        if lora_args.use_lora:
            self.model = self._apply_lora()
        
        logger.info(f"模型初始化完成: {model_args.model_name_or_path}")
    
    def _load_model(self) -> WhisperForConditionalGeneration:
        """加载Whisper模型"""
        model = WhisperForConditionalGeneration.from_pretrained(
            self.model_args.model_name_or_path,
            cache_dir=self.model_args.cache_dir,
            torch_dtype=torch.float16 if self.training_args.fp16 else torch.float32
        )
        
        model.train()
        return model
    
    def _apply_lora(self) -> PeftModel:
        """应用LoRA配置"""
        lora_config = LoraConfig(
            task_type=TaskType.FEATURE_EXTRACTION,
            r=self.lora_args.lora_r,
            lora_alpha=self.lora_args.lora_alpha,
            lora_dropout=self.lora_args.lora_dropout,
            target_modules=["q_proj", "k_proj", "v_proj", "out_proj", "fc1", "fc2"],
            bias="none"
        )
        
        model = get_peft_model(self.model, lora_config)
        model.print_trainable_parameters()
        
        logger.info("LoRA配置已应用")
        return model

def load_dataset_from_json(file_path: str) -> Dataset:
    """从JSON文件加载数据集"""
    with open(file_path, 'r', encoding='utf-8') as f:
        data = json.load(f)
    
    if not isinstance(data, list):
        raise ValueError("JSON文件应包含一个列表")
    
    required_fields = ['audio_path', 'text']
    for item in data:
        for field in required_fields:
            if field not in item:
                raise ValueError(f"缺少必需字段: {field}")
    
    dataset_dict = {
        'audio': [item['audio_path'] for item in data],
        'sentence': [item['text'] for item in data]
    }
    
    return Dataset.from_dict(dataset_dict)

def main():
    """主函数"""
    parser = argparse.ArgumentParser(description="Whisper LoRA微调")
    
    # 基本参数
    parser.add_argument("--model_name_or_path", type=str, 
                       default="openai/whisper-small",
                       help="Whisper模型路径")
    parser.add_argument("--train_file", type=str, required=True,
                       help="训练数据文件")
    parser.add_argument("--validation_file", type=str, default=None,
                       help="验证数据文件")
    parser.add_argument("--output_dir", type=str, required=True,
                       help="输出目录")
    parser.add_argument("--num_train_epochs", type=int, default=3,
                       help="训练轮数")
    parser.add_argument("--per_device_train_batch_size", type=int, default=4,
                       help="每设备训练批次大小")
    parser.add_argument("--learning_rate", type=float, default=5e-4,
                       help="学习率")
    parser.add_argument("--fp16", action="store_true",
                       help="使用FP16")
    
    args = parser.parse_args()
    
    # 创建参数对象
    model_args = ModelArguments(model_name_or_path=args.model_name_or_path)
    data_args = DataArguments(train_file=args.train_file, validation_file=args.validation_file)
    lora_args = LoraArguments()
    
    training_args = Seq2SeqTrainingArguments(
        output_dir=args.output_dir,
        num_train_epochs=args.num_train_epochs,
        per_device_train_batch_size=args.per_device_train_batch_size,
        learning_rate=args.learning_rate,
        fp16=args.fp16,
        logging_steps=50,
        save_steps=500,
        eval_steps=500,
        evaluation_strategy="steps",
        save_strategy="steps",
        predict_with_generate=True
    )
    
    # 创建训练器
    trainer = WhisperLoRATrainer(
        model_args=model_args,
        data_args=data_args,
        lora_args=lora_args,
        training_args=training_args
    )
    
    # 加载数据集
    logger.info(f"加载训练数据: {args.train_file}")
    train_dataset = load_dataset_from_json(args.train_file)
    
    eval_dataset = None
    if args.validation_file:
        logger.info(f"加载验证数据: {args.validation_file}")
        eval_dataset = load_dataset_from_json(args.validation_file)
    
    logger.info("训练完成!")

if __name__ == "__main__":
    main()