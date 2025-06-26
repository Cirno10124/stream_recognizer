#!/usr/bin/env python3
"""
PaddleOCR 3.0.0 稳定版参数配置管理器
支持从参数列表中动态取用配置参数
"""

import os
import sys
from typing import Dict, List, Optional, Any, Union
import json
import argparse

# 检查PaddleOCR可用性
try:
    from paddleocr import PaddleOCR
    PADDLEOCR_AVAILABLE = True
    print("✅ PaddleOCR可用")
except ImportError as e:
    PADDLEOCR_AVAILABLE = False
    print(f"❌ PaddleOCR不可用: {e}")
    # 为类型提示创建虚拟类
    class PaddleOCR:
        pass


class PaddleOCRConfig:
    """PaddleOCR 3.0.0 稳定版配置管理器"""
    
    def __init__(self):
        """初始化配置管理器"""
        self.supported_params = self._get_supported_params()
        self.default_configs = self._get_default_configs()
        self.ocr_instance = None
    
    def _get_supported_params(self) -> Dict[str, Dict]:
        """获取PaddleOCR 3.0.0支持的参数列表及其说明"""
        return {
            # 基础参数
            'lang': {
                'type': str,
                'default': 'ch',
                'description': '识别语言',
                'choices': ['ch', 'en', 'fr', 'german', 'korean', 'japan', 'chinese_cht']
            },
            'device': {
                'type': str,
                'default': 'cpu',
                'description': '运行设备',
                'choices': ['cpu', 'gpu']
            },
            'ocr_version': {
                'type': str,
                'default': 'PP-OCRv5',
                'description': 'OCR模型版本',
                'choices': ['PP-OCRv5', 'PP-OCRv4', 'PP-OCRv3']
            },
            
            # 模块开关
            'use_doc_orientation_classify': {
                'type': bool,
                'default': False,
                'description': '是否启用文档方向分类'
            },
            'use_doc_unwarping': {
                'type': bool,
                'default': False,
                'description': '是否启用文档展平'
            },
            'use_textline_orientation': {
                'type': bool,
                'default': True,
                'description': '是否启用文本行方向分类'
            },
            
            # 文本检测参数
            'text_det_limit_side_len': {
                'type': int,
                'default': 736,
                'description': '文本检测图像边长限制',
                'range': [320, 2048]
            },
            'text_det_limit_type': {
                'type': str,
                'default': 'min',
                'description': '边长限制类型',
                'choices': ['min', 'max']
            },
            'text_det_thresh': {
                'type': float,
                'default': 0.3,
                'description': '文本检测像素阈值',
                'range': [0.1, 0.9]
            },
            'text_det_box_thresh': {
                'type': float,
                'default': 0.6,
                'description': '文本检测框阈值',
                'range': [0.1, 0.9]
            },
            'text_det_unclip_ratio': {
                'type': float,
                'default': 1.5,
                'description': '文本检测扩展系数',
                'range': [1.0, 3.0]
            },
            
            # 文本识别参数
            'text_rec_score_thresh': {
                'type': float,
                'default': 0.0,
                'description': '文本识别分数阈值',
                'range': [0.0, 1.0]
            },
            'text_recognition_batch_size': {
                'type': int,
                'default': 1,
                'description': '文本识别批次大小',
                'range': [1, 32]
            },
            
            # 性能参数
            'enable_mkldnn': {
                'type': bool,
                'default': True,
                'description': '是否启用MKL-DNN加速'
            },
            'cpu_threads': {
                'type': int,
                'default': 8,
                'description': 'CPU线程数',
                'range': [1, 32]
            },
            'enable_hpi': {
                'type': bool,
                'default': False,
                'description': '是否启用高性能推理'
            },
            'use_tensorrt': {
                'type': bool,
                'default': False,
                'description': '是否使用TensorRT加速'
            },
            'precision': {
                'type': str,
                'default': 'fp32',
                'description': '计算精度',
                'choices': ['fp32', 'fp16', 'int8']
            },
            'mkldnn_cache_capacity': {
                'type': int,
                'default': 10,
                'description': 'MKL-DNN缓存容量',
                'range': [1, 100]
            }
        }
    
    def _get_default_configs(self) -> Dict[str, Dict]:
        """获取预定义的配置方案"""
        return {
            'fast': {
                'description': '快速模式 - 适合实时处理',
                'params': {
                    'lang': 'ch',
                    'device': 'gpu',
                    'ocr_version': 'PP-OCRv5',
                    'text_det_limit_side_len': 480,
                    'text_det_thresh': 0.4,
                    'text_det_box_thresh': 0.7,
                    'text_rec_score_thresh': 0.2,
                    'text_recognition_batch_size': 4,
                    'cpu_threads': 4,
                    'enable_mkldnn': True,
                    'use_textline_orientation': False
                }
            },
            'accurate': {
                'description': '精确模式 - 适合高质量识别',
                'params': {
                    'lang': 'ch',
                    'device': 'gpu',
                    'ocr_version': 'PP-OCRv5',
                    'text_det_limit_side_len': 960,
                    'text_det_thresh': 0.3,
                    'text_det_box_thresh': 0.6,
                    'text_det_unclip_ratio': 1.5,
                    'text_rec_score_thresh': 0.0,
                    'text_recognition_batch_size': 1,
                    'cpu_threads': 8,
                    'enable_mkldnn': True,
                    'use_textline_orientation': True
                }
            },
            'balanced': {
                'description': '平衡模式 - 速度和精度兼顾',
                'params': {
                    'lang': 'ch',
                    'device': 'gpu',
                    'ocr_version': 'PP-OCRv5',
                    'text_det_limit_side_len': 736,
                    'text_det_thresh': 0.3,
                    'text_det_box_thresh': 0.6,
                    'text_det_unclip_ratio': 1.5,
                    'text_rec_score_thresh': 0.1,
                    'text_recognition_batch_size': 2,
                    'cpu_threads': 8,
                    'enable_mkldnn': True,
                    'use_textline_orientation': True
                }
            },
            'subtitle': {
                'description': '字幕识别模式 - 专为字幕优化',
                'params': {
                    'lang': 'ch',
                    'device': 'gpu',
                    'ocr_version': 'PP-OCRv5',
                    'text_det_limit_side_len': 960,
                    'text_det_thresh': 0.2,
                    'text_det_box_thresh': 0.5,
                    'text_det_unclip_ratio': 2.0,
                    'text_rec_score_thresh': 0.0,
                    'text_recognition_batch_size': 1,
                    'cpu_threads': 8,
                    'enable_mkldnn': True,
                    'use_textline_orientation': True,
                    'use_doc_orientation_classify': False,
                    'use_doc_unwarping': False
                }
            }
        }
    
    def get_param_info(self, param_name: Optional[str] = None) -> Dict:
        """获取参数信息"""
        if param_name:
            return self.supported_params.get(param_name, {})
        return self.supported_params
    
    def validate_config(self, config: Dict) -> Dict:
        """验证并修正配置参数"""
        validated_config = {}
        errors = []
        
        for param_name, param_value in config.items():
            param_info = self.supported_params.get(param_name)
            
            if not param_info:
                errors.append(f"不支持的参数: {param_name}")
                continue
            
            # 类型检查
            expected_type = param_info['type']
            if not isinstance(param_value, expected_type):
                try:
                    if expected_type == bool:
                        param_value = bool(param_value)
                    elif expected_type == int:
                        param_value = int(param_value)
                    elif expected_type == float:
                        param_value = float(param_value)
                    elif expected_type == str:
                        param_value = str(param_value)
                except (ValueError, TypeError):
                    errors.append(f"参数 {param_name} 类型错误，期望 {expected_type.__name__}")
                    continue
            
            # 范围检查
            if 'range' in param_info:
                min_val, max_val = param_info['range']
                if not (min_val <= param_value <= max_val):
                    errors.append(f"参数 {param_name} 超出范围 [{min_val}, {max_val}]")
                    continue
            
            # 选择检查
            if 'choices' in param_info:
                if param_value not in param_info['choices']:
                    errors.append(f"参数 {param_name} 无效选择，可选值: {param_info['choices']}")
                    continue
            
            validated_config[param_name] = param_value
        
        return {'config': validated_config, 'errors': errors}
    
    def get_config_from_preset(self, preset_name: str) -> Dict:
        """从预设配置获取参数"""
        if preset_name not in self.default_configs:
            return {}
        return self.default_configs[preset_name]['params'].copy()
    
    def merge_configs(self, base_config: Dict, override_config: Dict) -> Dict:
        """合并配置，override_config会覆盖base_config"""
        merged = base_config.copy()
        merged.update(override_config)
        return merged
    
    def create_ocr_instance(self, config: Dict) -> Optional[Any]:
        """根据配置创建PaddleOCR实例"""
        if not PADDLEOCR_AVAILABLE:
            print("❌ PaddleOCR不可用，请先安装PaddleOCR")
            print("运行: python install_paddleocr_stable.py")
            return None
        
        # 验证配置
        validation_result = self.validate_config(config)
        if validation_result['errors']:
            print("配置验证错误:")
            for error in validation_result['errors']:
                print(f"  - {error}")
            return None
        
        validated_config = validation_result['config']
        
        # 检查GPU可用性
        if validated_config.get('device') == 'gpu':
            try:
                import paddle
                cuda_available = paddle.device.is_compiled_with_cuda()
                gpu_count = paddle.device.cuda.device_count()
                if not (cuda_available and gpu_count > 0):
                    print("警告：GPU不可用，将使用CPU")
                    validated_config['device'] = 'cpu'
            except:
                print("警告：无法检测GPU，将使用CPU")
                validated_config['device'] = 'cpu'
        
        try:
            print(f"正在初始化PaddleOCR，配置: {len(validated_config)} 个参数")
            # 只有在PaddleOCR可用时才导入
            from paddleocr import PaddleOCR as RealPaddleOCR
            self.ocr_instance = RealPaddleOCR(**validated_config)
            print("✅ PaddleOCR初始化成功")
            return self.ocr_instance
        except Exception as e:
            print(f"❌ PaddleOCR初始化失败: {e}")
            return None
    
    def print_supported_params(self):
        """打印所有支持的参数"""
        print("\n=== PaddleOCR 3.0.0 稳定版支持的参数 ===")
        
        categories = {
            '基础参数': ['lang', 'device', 'ocr_version'],
            '模块开关': ['use_doc_orientation_classify', 'use_doc_unwarping', 'use_textline_orientation'],
            '文本检测': ['text_det_limit_side_len', 'text_det_limit_type', 'text_det_thresh', 
                      'text_det_box_thresh', 'text_det_unclip_ratio'],
            '文本识别': ['text_rec_score_thresh', 'text_recognition_batch_size'],
            '性能优化': ['enable_mkldnn', 'cpu_threads', 'enable_hpi', 'use_tensorrt', 
                      'precision', 'mkldnn_cache_capacity']
        }
        
        for category, params in categories.items():
            print(f"\n{category}:")
            for param in params:
                info = self.supported_params.get(param, {})
                default = info.get('default', 'N/A')
                desc = info.get('description', '')
                param_type = info.get('type', type).__name__
                
                print(f"  {param} ({param_type}): {desc}")
                print(f"    默认值: {default}")
                
                if 'choices' in info:
                    print(f"    可选值: {info['choices']}")
                elif 'range' in info:
                    print(f"    范围: {info['range']}")
                print()
    
    def print_presets(self):
        """打印预设配置"""
        print("\n=== 预设配置方案 ===")
        for preset_name, preset_info in self.default_configs.items():
            print(f"\n{preset_name}: {preset_info['description']}")
            print("参数:")
            for param, value in preset_info['params'].items():
                print(f"  {param}: {value}")


def main():
    """主函数"""
    parser = argparse.ArgumentParser(description='PaddleOCR 3.0.0 稳定版配置管理器')
    parser.add_argument('--show-params', action='store_true', help='显示所有支持的参数')
    parser.add_argument('--show-presets', action='store_true', help='显示预设配置')
    parser.add_argument('--preset', type=str, help='使用预设配置')
    parser.add_argument('--test-init', action='store_true', help='测试初始化OCR实例')
    parser.add_argument('--device', type=str, choices=['cpu', 'gpu'], help='指定设备')
    parser.add_argument('--lang', type=str, help='指定识别语言')
    
    args = parser.parse_args()
    
    # 创建配置管理器
    config_manager = PaddleOCRConfig()
    
    # 显示参数信息
    if args.show_params:
        config_manager.print_supported_params()
        return
    
    # 显示预设配置
    if args.show_presets:
        config_manager.print_presets()
        return
    
    # 构建配置
    config = {}
    
    # 从预设加载
    if args.preset:
        config = config_manager.get_config_from_preset(args.preset)
        if not config:
            print(f"未找到预设配置: {args.preset}")
            return
        print(f"使用预设配置: {args.preset}")
    
    # 命令行参数覆盖
    overrides = {}
    if args.device:
        overrides['device'] = args.device
    if args.lang:
        overrides['lang'] = args.lang
    
    if overrides:
        config = config_manager.merge_configs(config, overrides)
    
    # 如果没有配置，使用默认的平衡模式
    if not config:
        config = config_manager.get_config_from_preset('balanced')
        print("使用默认配置: balanced")
    
    print(f"\n当前配置 ({len(config)} 个参数):")
    for key, value in config.items():
        print(f"  {key}: {value}")
    
    # 测试初始化
    if args.test_init:
        print("\n正在测试OCR实例初始化...")
        ocr_instance = config_manager.create_ocr_instance(config)
        if ocr_instance:
            print("🎉 OCR实例创建成功!")
        else:
            print("💥 OCR实例创建失败")


if __name__ == "__main__":
    main()
 