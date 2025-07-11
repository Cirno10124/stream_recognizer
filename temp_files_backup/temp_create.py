#!/usr/bin/env python3
# 创建PaddleOCR提取器文件

content = '''#!/usr/bin/env python3
"""
改进的PaddleOCR字幕提取器 - 支持3.0.0稳定版参数化配置
"""

import os
import cv2
import numpy as np
from pathlib import Path
from typing import List, Dict, Optional, Tuple
import argparse

# 检查PaddleOCR可用性
try:
    from paddleocr import PaddleOCR
    PADDLEOCR_AVAILABLE = True
    print("✅ PaddleOCR可用")
except ImportError as e:
    PADDLEOCR_AVAILABLE = False
    print(f"❌ PaddleOCR不可用: {e}")


class PaddleOCRExtractor:
    """基于PaddleOCR 3.0.0的字幕提取器"""
    
    def __init__(self, 
                 use_gpu: bool = True,
                 ocr_params: Dict = None):
        """
        初始化PaddleOCR提取器
        
        Args:
            use_gpu: 是否使用GPU加速
            ocr_params: PaddleOCR参数字典，覆盖默认参数
        """
        if not PADDLEOCR_AVAILABLE:
            raise RuntimeError("PaddleOCR未安装或版本不兼容")
            
        self.use_gpu = use_gpu
        self.ocr_params = ocr_params or {}
        self.ocr_engine = None
        
        # 初始化OCR引擎
        self._initialize_paddleocr()
        
    def _get_default_paddleocr_params(self) -> Dict:
        """获取PaddleOCR 3.0.0稳定版的默认参数"""
        default_params = {
            # 核心设置
            'lang': 'ch',  # 中文识别
            'ocr_version': 'PP-OCRv5',  # 使用最新的PP-OCRv5模型
            
            # 模块开关
            'use_doc_orientation_classify': False,  # 文档方向分类
            'use_doc_unwarping': False,  # 文档展平
            'use_textline_orientation': True,  # 文本行方向分类
            
            # 文本检测参数
            'text_det_limit_side_len': 736,  # 图像边长限制
            'text_det_limit_type': 'min',  # 边长限制类型
            'text_det_thresh': 0.3,  # 像素阈值
            'text_det_box_thresh': 0.6,  # 框阈值
            'text_det_unclip_ratio': 1.5,  # 扩展系数
            
            # 文本识别参数
            'text_rec_score_thresh': 0.0,  # 识别分数阈值
            'text_recognition_batch_size': 1,  # 批次大小
            
            # 性能参数
            'enable_mkldnn': True,  # MKL-DNN加速
            'cpu_threads': 8,  # CPU线程数
            'enable_hpi': False,  # 高性能推理
            'use_tensorrt': False,  # TensorRT加速 
            'precision': 'fp32',  # 计算精度
            'mkldnn_cache_capacity': 10,  # MKL-DNN缓存容量
        }
        
        # 用自定义参数覆盖默认参数
        default_params.update(self.ocr_params)
        
        return default_params

    def _initialize_paddleocr(self) -> bool:
        """初始化PaddleOCR 3.0.0稳定版"""
        try:
            # 检查CUDA是否可用
            try:
                import paddle
                cuda_available = paddle.device.is_compiled_with_cuda()
                gpu_count = paddle.device.cuda.device_count()
                print(f"CUDA可用: {cuda_available}, GPU数量: {gpu_count}")
            except:
                cuda_available = False
                gpu_count = 0
            
            # 获取完整的参数配置
            paddleocr_params = self._get_default_paddleocr_params()
            
            # 根据GPU可用性调整设备设置
            if self.use_gpu and cuda_available and gpu_count > 0:
                paddleocr_params['device'] = 'gpu'
                print("使用GPU加速的PaddleOCR 3.0.0")
            else:
                paddleocr_params['device'] = 'cpu'
                print("使用CPU版本的PaddleOCR 3.0.0")
                if self.use_gpu:
                    print("警告：请求使用GPU但未检测到CUDA支持，将使用CPU")
            
            # 过滤出PaddleOCR构造函数支持的参数
            valid_params = self._filter_valid_paddleocr_params(paddleocr_params)
            
            print(f"PaddleOCR初始化参数: {list(valid_params.keys())}")
            
            # 初始化PaddleOCR
            self.ocr_engine = PaddleOCR(**valid_params)
            
            print("✅ PaddleOCR 3.0.0初始化成功")
            return True
            
        except Exception as e:
            print(f"❌ PaddleOCR初始化失败: {e}")
            import traceback
            traceback.print_exc()
            return False
    
    def _filter_valid_paddleocr_params(self, params: Dict) -> Dict:
        """过滤出PaddleOCR构造函数支持的参数"""
        # PaddleOCR 3.0.0支持的参数列表
        valid_param_names = {
            # 基础参数
            'lang', 'device', 'ocr_version',
            
            # 模块开关参数
            'use_doc_orientation_classify', 'use_doc_unwarping', 'use_textline_orientation',
            
            # 模型名称参数
            'doc_orientation_classify_model_name', 'doc_unwarping_model_name',
            'text_detection_model_name', 'textline_orientation_model_name', 
            'text_recognition_model_name',
            
            # 模型路径参数
            'doc_orientation_classify_model_dir', 'doc_unwarping_model_dir',
            'text_detection_model_dir', 'textline_orientation_model_dir', 
            'text_recognition_model_dir',
            
            # 检测参数
            'text_det_limit_side_len', 'text_det_limit_type', 'text_det_thresh',
            'text_det_box_thresh', 'text_det_unclip_ratio',
            
            # 识别参数
            'text_rec_score_thresh', 'text_recognition_batch_size',
            'textline_orientation_batch_size',
            
            # 性能参数
            'enable_mkldnn', 'cpu_threads', 'enable_hpi', 'use_tensorrt',
            'precision', 'mkldnn_cache_capacity',
        }
        
        # 过滤有效参数
        filtered_params = {k: v for k, v in params.items() if k in valid_param_names}
        
        # 打印被过滤掉的参数（调试用）
        filtered_out = set(params.keys()) - set(filtered_params.keys())
        if filtered_out:
            print(f"过滤掉的参数: {filtered_out}")
        
        return filtered_params
    
    def get_available_params(self) -> Dict:
        """获取可用的PaddleOCR参数列表"""
        return self._get_default_paddleocr_params()
    
    def print_param_info(self):
        """打印PaddleOCR参数信息"""
        params = self._get_default_paddleocr_params()
        
        print("\\n=== PaddleOCR 3.0.0 参数说明 ===")
        print("核心设置:")
        print(f"  lang: {params['lang']} - 识别语言")
        print(f"  ocr_version: {params['ocr_version']} - OCR模型版本")
        print(f"  device: auto - 设备选择 (gpu/cpu)")
        
        print("\\n模块开关:")
        print(f"  use_doc_orientation_classify: {params['use_doc_orientation_classify']} - 文档方向分类")
        print(f"  use_doc_unwarping: {params['use_doc_unwarping']} - 文档展平")
        print(f"  use_textline_orientation: {params['use_textline_orientation']} - 文本行方向分类")
        
        print("\\n文本检测参数:")
        print(f"  text_det_limit_side_len: {params['text_det_limit_side_len']} - 图像边长限制")
        print(f"  text_det_limit_type: {params['text_det_limit_type']} - 边长限制类型")
        print(f"  text_det_thresh: {params['text_det_thresh']} - 像素阈值")
        print(f"  text_det_box_thresh: {params['text_det_box_thresh']} - 框阈值")
        print(f"  text_det_unclip_ratio: {params['text_det_unclip_ratio']} - 扩展系数")
        
        print("\\n文本识别参数:")
        print(f"  text_rec_score_thresh: {params['text_rec_score_thresh']} - 识别分数阈值")
        print(f"  text_recognition_batch_size: {params['text_recognition_batch_size']} - 批次大小")
        
        print("\\n性能参数:")
        print(f"  enable_mkldnn: {params['enable_mkldnn']} - MKL-DNN加速")
        print(f"  cpu_threads: {params['cpu_threads']} - CPU线程数")
        print(f"  enable_hpi: {params['enable_hpi']} - 高性能推理")
        print(f"  use_tensorrt: {params['use_tensorrt']} - TensorRT加速")
        print(f"  precision: {params['precision']} - 计算精度")
        print(f"  mkldnn_cache_capacity: {params['mkldnn_cache_capacity']} - MKL-DNN缓存容量")


def main():
    """主函数"""
    parser = argparse.ArgumentParser(description='PaddleOCR 3.0.0 字幕提取器')
    parser.add_argument('--show-params', action='store_true', help='显示参数信息')
    parser.add_argument('--ocr-lang', default='ch', help='OCR识别语言')
    parser.add_argument('--ocr-version', default='PP-OCRv5', help='OCR模型版本')
    
    args = parser.parse_args()
    
    # 自定义OCR参数
    custom_ocr_params = {
        'lang': args.ocr_lang,
        'ocr_version': args.ocr_version,
    }
    
    # 创建提取器
    extractor = PaddleOCRExtractor(
        use_gpu=True,
        ocr_params=custom_ocr_params
    )
    
    # 显示参数信息
    if args.show_params:
        extractor.print_param_info()
        return


if __name__ == "__main__":
    main()
'''

# 创建文件
with open('subtitle_wer_tool/improved_paddleocr_extractor.py', 'w', encoding='utf-8') as f:
    f.write(content)

print("✅ 文件创建成功：subtitle_wer_tool/improved_paddleocr_extractor.py")
print("已创建基于PaddleOCR 3.0.0稳定版的字幕提取器")
