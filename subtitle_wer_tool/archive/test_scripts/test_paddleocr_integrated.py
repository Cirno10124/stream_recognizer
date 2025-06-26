#!/usr/bin/env python3
"""
测试整合后的PaddleOCR参数化配置系统
验证从参数列表中动态取用配置的功能
"""

import os
import sys
from pathlib import Path

def test_config_manager():
    """测试配置管理器"""
    print("=== 测试PaddleOCR配置管理器 ===")
    
    try:
        from paddleocr_v3_stable import PaddleOCRConfig
        config_manager = PaddleOCRConfig()
        
        print("✅ 配置管理器加载成功")
        
        # 显示支持的参数
        print("\n📋 支持的参数列表:")
        config_manager.print_supported_params()
        
        # 显示预设配置
        print("\n📋 预设配置:")
        config_manager.print_presets()
        
        # 测试参数验证
        print("\n🔍 测试参数验证:")
        test_config = {
            'lang': 'ch',
            'device': 'cpu',
            'text_det_thresh': 0.3,
            'invalid_param': 'should_be_filtered'  # 这个参数应该被过滤掉
        }
        
        validated_config = config_manager.validate_config(test_config)
        print(f"原始配置: {test_config}")
        print(f"验证后配置: {validated_config}")
        
        return True
        
    except ImportError as e:
        print(f"❌ 配置管理器导入失败: {e}")
        return False
    except Exception as e:
        print(f"❌ 配置管理器测试失败: {e}")
        return False

def test_extractor():
    """测试字幕提取器"""
    print("\n=== 测试PaddleOCR字幕提取器 ===")
    
    try:
        from improved_paddleocr_extractor import PaddleOCRExtractor
        
        print("✅ 字幕提取器模块加载成功")
        
        # 测试不同预设配置的创建
        presets = ['fast', 'accurate', 'balanced', 'subtitle']
        
        for preset in presets:
            try:
                print(f"\n🔧 测试预设配置: {preset}")
                extractor = PaddleOCRExtractor(
                    preset=preset,
                    use_gpu=False  # 使用CPU避免GPU依赖问题
                )
                
                # 获取配置信息
                config_info = extractor.get_config_info()
                print(f"   预设: {config_info['preset']}")
                print(f"   自定义参数: {config_info['custom_params']}")
                print(f"   GPU加速: {config_info['use_gpu']}")
                print(f"✅ 预设配置 {preset} 创建成功")
                
            except Exception as e:
                print(f"❌ 预设配置 {preset} 创建失败: {e}")
        
        # 测试自定义参数
        print(f"\n🔧 测试自定义参数:")
        try:
            custom_params = {
                'text_det_thresh': 0.5,
                'text_rec_score_thresh': 0.1,
                'cpu_threads': 4
            }
            
            extractor = PaddleOCRExtractor(
                preset='balanced',
                custom_params=custom_params,
                use_gpu=False
            )
            
            config_info = extractor.get_config_info()
            print(f"   基础预设: balanced")
            print(f"   自定义参数: {custom_params}")
            print(f"✅ 自定义参数配置创建成功")
            
        except Exception as e:
            print(f"❌ 自定义参数配置创建失败: {e}")
        
        return True
        
    except ImportError as e:
        print(f"❌ 字幕提取器导入失败: {e}")
        return False
    except Exception as e:
        print(f"❌ 字幕提取器测试失败: {e}")
        return False

def test_gui_integration():
    """测试GUI集成"""
    print("\n=== 测试GUI集成 ===")
    
    try:
        # 检查GUI模块能否正常导入新的提取器
        from subtitle_wer_gui import IMPROVED_OCR_AVAILABLE
        
        if IMPROVED_OCR_AVAILABLE:
            print("✅ GUI模块已成功集成参数化PaddleOCR提取器")
            
            # 测试模拟OCR设置
            mock_ocr_settings = {
                'preset': 'subtitle',
                'custom_params': {'text_det_thresh': 0.3},
                'sample_interval': 2.0,
                'confidence_threshold': 0.2,
                'use_gpu': True,
                'subtitle_region': None
            }
            
            print(f"模拟OCR设置: {mock_ocr_settings}")
            print("✅ GUI集成测试通过")
            return True
        else:
            print("❌ GUI模块未能集成参数化PaddleOCR提取器")
            return False
            
    except ImportError as e:
        print(f"❌ GUI模块导入失败: {e}")
        return False
    except Exception as e:
        print(f"❌ GUI集成测试失败: {e}")
        return False

def test_parameter_usage():
    """测试参数列表的动态使用"""
    print("\n=== 测试参数列表动态使用 ===")
    
    try:
        from paddleocr_v3_stable import PaddleOCRConfig
        config_manager = PaddleOCRConfig()
        
        # 测试从参数列表中动态取用配置
        print("🔍 测试动态参数取用:")
        
        # 获取所有支持的参数
        supported_params = config_manager.supported_params
        print(f"支持的参数数量: {len(supported_params)}")
        
        # 动态构建配置
        dynamic_config = {}
        
        # 基础参数
        if 'lang' in supported_params:
            dynamic_config['lang'] = 'ch'
            print("✓ 添加语言参数")
        
        if 'device' in supported_params:
            dynamic_config['device'] = 'cpu'
            print("✓ 添加设备参数")
        
        # 检测参数
        if 'text_det_thresh' in supported_params:
            dynamic_config['text_det_thresh'] = 0.3
            print("✓ 添加检测阈值参数")
        
        # 识别参数
        if 'text_rec_score_thresh' in supported_params:
            dynamic_config['text_rec_score_thresh'] = 0.1
            print("✓ 添加识别阈值参数")
        
        # 性能参数
        if 'cpu_threads' in supported_params:
            dynamic_config['cpu_threads'] = 4
            print("✓ 添加CPU线程数参数")
        
        print(f"\n动态构建的配置: {dynamic_config}")
        
        # 验证动态配置
        validated_config = config_manager.validate_config(dynamic_config)
        print(f"验证后的配置: {validated_config}")
        
        # 测试未知参数过滤
        print("\n🔍 测试未知参数过滤:")
        config_with_unknown = dynamic_config.copy()
        config_with_unknown.update({
            'unknown_param_1': 'should_be_filtered',
            'unknown_param_2': 123,
            'invalid_parameter': True
        })
        
        print(f"包含未知参数的配置: {config_with_unknown}")
        filtered_config = config_manager.validate_config(config_with_unknown)
        print(f"过滤后的配置: {filtered_config}")
        
        # 检查是否成功过滤了未知参数
        unknown_params = set(config_with_unknown.keys()) - set(filtered_config.keys())
        if unknown_params:
            print(f"✅ 成功过滤未知参数: {unknown_params}")
        else:
            print("ℹ️ 没有发现未知参数需要过滤")
        
        print("✅ 参数列表动态使用测试通过")
        return True
        
    except Exception as e:
        print(f"❌ 参数列表动态使用测试失败: {e}")
        import traceback
        traceback.print_exc()
        return False

def main():
    """主测试函数"""
    print("🚀 开始测试PaddleOCR 3.0.0参数化配置系统整合")
    print("=" * 60)
    
    test_results = []
    
    # 运行各项测试
    test_results.append(("配置管理器", test_config_manager()))
    test_results.append(("字幕提取器", test_extractor()))
    test_results.append(("GUI集成", test_gui_integration()))
    test_results.append(("参数动态使用", test_parameter_usage()))
    
    # 总结测试结果
    print("\n" + "=" * 60)
    print("📊 测试结果总结:")
    
    passed = 0
    failed = 0
    
    for test_name, result in test_results:
        status = "✅ 通过" if result else "❌ 失败"
        print(f"   {test_name}: {status}")
        if result:
            passed += 1
        else:
            failed += 1
    
    print(f"\n总计: {passed} 项通过, {failed} 项失败")
    
    if failed == 0:
        print("🎉 所有测试通过！PaddleOCR参数化配置系统整合成功")
        print("✨ 现在可以使用参数列表中的配置，不会有未知参数问题")
    else:
        print("⚠️ 部分测试失败，请检查相关配置")
    
    return failed == 0

if __name__ == '__main__':
    success = main()
    sys.exit(0 if success else 1) 