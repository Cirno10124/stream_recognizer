#!/usr/bin/env python3
"""
最简单的PaddleOCR测试
"""

def test_paddleocr_basic():
    """基础PaddleOCR测试"""
    print("开始PaddleOCR测试...")
    
    try:
        print("1. 导入PaddleOCR...")
        from paddleocr import PaddleOCR
        
        print("2. 检查CUDA...")
        try:
            import paddle
            cuda_available = paddle.device.is_compiled_with_cuda()
            gpu_count = paddle.device.cuda.device_count()
            print(f"   CUDA可用: {cuda_available}, GPU数量: {gpu_count}")
        except Exception as e:
            print(f"   CUDA检查错误: {e}")
            cuda_available = False
            gpu_count = 0
        
        print("3. 初始化PaddleOCR...")
        device = 'gpu' if cuda_available and gpu_count > 0 else 'cpu'
        print(f"   使用设备: {device}")
        
        # 最简单的初始化
        ocr = PaddleOCR(
            use_textline_orientation=True,
            lang='ch',
            device=device
        )
        
        print("✅ PaddleOCR初始化成功!")
        return True
        
    except Exception as e:
        print(f"❌ PaddleOCR测试失败: {e}")
        import traceback
        traceback.print_exc()
        return False

if __name__ == "__main__":
    success = test_paddleocr_basic()
    if success:
        print("\n🎉 PaddleOCR工作正常!")
    else:
        print("\n💥 PaddleOCR存在问题") 