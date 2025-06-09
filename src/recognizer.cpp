#include <audio_processor.h>
#include <whisper.h>
#include <thread>
#include <chrono>
#include <iostream>
#include <windows.h> // 添加Windows API头文件
#include <algorithm> 
#include <filesystem>
#include <mutex> 
// FastRecognizer实现
FastRecognizer::FastRecognizer(const std::string& model_path, ResultQueue* input_queue,
                              const std::string& language, bool use_gpu, float vad_threshold)
    : model_path(model_path),
      input_queue(input_queue),
      output_queue(nullptr),
      language(language),
      use_gpu(use_gpu),
      vad_threshold(vad_threshold),
      ctx(nullptr) {
    
    // 使用静态mutex确保whisper初始化的线程安全
    static std::mutex whisper_init_mutex;
    std::lock_guard<std::mutex> lock(whisper_init_mutex);
    
    try {
        // 验证模型路径
        if (model_path.empty()) {
            throw std::runtime_error("Model path is empty");
        }
        
        // 检查模型文件是否存在
        if (!std::filesystem::exists(model_path)) {
            throw std::runtime_error("Model file not found: " + model_path);
        }
        
        // 检查文件大小
        std::error_code ec;
        auto file_size = std::filesystem::file_size(model_path, ec);
        if (ec || file_size < 1024) {
            throw std::runtime_error("Invalid or corrupt model file: " + model_path);
        }
        
        std::cout << "Initializing FastRecognizer with model: " << model_path << std::endl;
        
    // 在构造函数中加载模型
    struct whisper_context_params params = whisper_context_default_params();
    
        // 安全地配置参数
        params.use_gpu = use_gpu;
        params.flash_attn = false;  // 禁用flash attention以避免兼容性问题
        params.dtw_token_timestamps = false;  // 禁用DTW以减少内存使用
        
    if (use_gpu) {
        params.gpu_device = 0;  // 使用第一个 GPU 设备
            std::cout << "GPU acceleration enabled, device ID: 0" << std::endl;
        } else {
            std::cout << "Using CPU mode" << std::endl;
        }
        
        // 分步骤初始化whisper context以便更好的错误处理
        std::cout << "Loading whisper model..." << std::endl;
        
        // 第一次尝试：使用指定设置
        ctx = whisper_init_from_file_with_params(model_path.c_str(), params);
        
        if (!ctx && use_gpu) {
            // GPU模式失败，尝试CPU回退
            std::cout << "GPU initialization failed, trying CPU fallback..." << std::endl;
        params.use_gpu = false;
    ctx = whisper_init_from_file_with_params(model_path.c_str(), params);
            
            if (ctx) {
                this->use_gpu = false;  // 更新状态
                std::cout << "Successfully loaded model in CPU fallback mode" << std::endl;
            }
        }
        
    if (!ctx) {
            throw std::runtime_error("Failed to initialize whisper context from model: " + model_path);
    }
    
    std::cout << "Fast recognition model loaded successfully: " << model_path 
                  << (this->use_gpu ? " (GPU enabled)" : " (CPU mode)") << std::endl;
                  
    } catch (const std::exception& e) {
        // 确保清理任何部分初始化的资源
        if (ctx) {
            whisper_free(ctx);
            ctx = nullptr;
        }
        
        std::cerr << "FastRecognizer initialization failed: " << e.what() << std::endl;
        throw std::runtime_error("Failed to initialize FastRecognizer: " + std::string(e.what()));
    } catch (...) {
        // 处理未知异常
        if (ctx) {
            whisper_free(ctx);
            ctx = nullptr;
        }
        
        std::cerr << "FastRecognizer initialization failed with unknown error" << std::endl;
        throw std::runtime_error("Failed to initialize FastRecognizer: unknown error");
    }
}

FastRecognizer::~FastRecognizer() {
    if (ctx) {
        whisper_free(ctx);
    }
}

void FastRecognizer::start() {
    running = true;
}

void FastRecognizer::stop() {
    running = false;
}

void FastRecognizer::process_audio_batch(const std::vector<AudioBuffer>& batch) {
    if (batch.empty()) {
        std::cerr << "Empty batch, skipping" << std::endl;
        return;
    }
    
    if (!ctx) {
        std::cerr << "Model not loaded, skipping" << std::endl;
        return;
    }
    
    // 合并所有缓冲区
    std::vector<float> combined_data;
    for (const auto& buffer : batch) {
        combined_data.insert(combined_data.end(), buffer.data.begin(), buffer.data.end());
    }
    
    // 检查音频长度是否足够
    float audio_length_ms = combined_data.size() * 1000.0f / 16000;
    const float min_audio_length_ms = 1000.0f; // 最小音频长度为1秒
    
    if (audio_length_ms < min_audio_length_ms) {
        std::cout << "音频过短，添加静音填充：" << audio_length_ms << "ms < " << min_audio_length_ms << "ms" << std::endl;
        
        // 计算需要添加的静音样本数量
        size_t padding_samples = (min_audio_length_ms - audio_length_ms) * 16000 / 1000;
        
        // 添加静音填充到音频末尾
        combined_data.insert(combined_data.end(), padding_samples, 0.0f);
        
        // 更新音频长度
        audio_length_ms = combined_data.size() * 1000.0f / 16000;
        std::cout << "填充后音频长度：" << audio_length_ms << "ms，样本数：" << combined_data.size() << std::endl;
    }
    
    // 设置whisper_full参数
    whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    
    // 设置语言
    if (language == "auto") {
        wparams.language = nullptr;
        wparams.detect_language = true;
    } else if (language == "en"||language=="English") {
        wparams.language = "en";
    } else if (language == "zh"||language=="Chinese") {
        wparams.language = "zh";
    } else if (language == "ja") {
        wparams.language = "ja";
    } else {
        wparams.language = nullptr;//默认为auto
    }
    
    // 其他参数设置
    wparams.n_threads = std::thread::hardware_concurrency();
    wparams.translate = false;
    wparams.print_progress = false;
    wparams.print_special = false;
    wparams.print_realtime = false;
    wparams.print_timestamps = false;
    wparams.no_context = true;
    wparams.single_segment = true;
    wparams.max_len = 0;
    wparams.token_timestamps = false;
    wparams.thold_pt = vad_threshold;
    wparams.entropy_thold = 2.7f;
    wparams.logprob_thold = -1.0f;
    
    auto recstart = std::chrono::high_resolution_clock::now();
    // 执行识别时使用显式类型转换
    if (whisper_full(ctx, wparams, combined_data.data(), static_cast<int>(combined_data.size())) != 0) {
        std::cerr << "Fast recognition failed" << std::endl;
        return;
    }
     
    auto recend = std::chrono::high_resolution_clock::now();
    auto rectime = std::chrono::duration_cast<std::chrono::milliseconds>(recend - recstart).count();
    
    const int n_segments = whisper_full_n_segments(ctx);
    
    if (n_segments == 0) {
        std::cout << "No speech detected" << std::endl;
        return;
    }
    
    RecognitionResult result;
    result.timestamp = batch.front().timestamp;
    
    std::string text = "";
    for (int i = 0; i < n_segments; ++i) {
        const char* segment_text = whisper_full_get_segment_text(ctx, i);
        text += segment_text;
    }
    
    // 过滤文本，移除特殊标记
    std::string filtered_text = text;
    std::vector<std::pair<std::string, std::string>> replace_patterns = {
        {"[音乐]", ""}, {"[掌声]", ""}, {"[笑声]", ""},
        {"[Music]", ""}, {"[Applause]", ""}, {"[Laughter]", ""},
        {"[MUSIC]", ""}, {"[APPLAUSE]", ""}, {"[LAUGHTER]", ""},
        {"*", ""}, {"\n", " "}
    };
    
    for (const auto& pattern : replace_patterns) {
        size_t pos = 0;
        while ((pos = filtered_text.find(pattern.first, pos)) != std::string::npos) {
            filtered_text.replace(pos, pattern.first.length(), pattern.second);
            pos += pattern.second.length();
        }
    }
    
    result.text = filtered_text;
    
    // 添加编码转换
    // 检测文本是否有编码问题，例如："浠栧彲鏄湪濂芥鍟"这种情况
    bool needs_conversion = false;
    // 检查是否包含乱码特征（对于UTF-8编码的中文来说，乱码通常表现为大量拉丁字母）
    int latin_char_count = 0;
    for (char c : filtered_text) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
            latin_char_count++;
        }
    }
    // 如果拉丁字母占比超过70%且文本长度足够长，很可能是编码问题
    if (latin_char_count > filtered_text.length() * 0.7 && filtered_text.length() > 10) {
        needs_conversion = true;
    }
    
    if (needs_conversion) {
        try {
            // 使用Windows API进行GBK到UTF-8的转换
            std::string utf8_text;
            
            // 1. 首先将GBK编码的字符串转换为宽字符(UTF-16)
            int wstr_size = MultiByteToWideChar(
                936, // CP_ACP for GBK/GB2312
                0,
                filtered_text.c_str(),
                -1, // 以null结尾的字符串
                NULL,
                0  // 获取所需大小
            );
            
            if (wstr_size > 0) {
                std::wstring wide_text(wstr_size, 0);
                MultiByteToWideChar(
                    936,
                    0,
                    filtered_text.c_str(),
                    -1,
                    &wide_text[0],
                    wstr_size
                );
                
                // 移除末尾的null字符
                if (!wide_text.empty() && wide_text.back() == L'\0') {
                    wide_text.pop_back();
                }
                
                // 2. 然后将UTF-16转换为UTF-8
                int utf8_size = WideCharToMultiByte(
                    CP_UTF8,
                    0,
                    wide_text.c_str(),
                    -1,
                    NULL,
                    0,
                    NULL,
                    NULL
                );
                
                if (utf8_size > 0) {
                    std::string converted_text(utf8_size, 0);
                    WideCharToMultiByte(
                        CP_UTF8,
                        0,
                        wide_text.c_str(),
                        -1,
                        &converted_text[0],
                        utf8_size,
                        NULL,
                        NULL
                    );
                    
                    // 移除末尾的null字符
                    if (!converted_text.empty() && converted_text.back() == '\0') {
                        converted_text.pop_back();
                    }
                    
                    utf8_text = converted_text;
                }
            }
            
            // 使用转换后的文本
            if (!utf8_text.empty()) {
                result.text = utf8_text;
                std::cout << "快速识别编码转换成功: " << utf8_text << std::endl;
            } else {
                // 如果转换后为空，使用原始文本
                result.text = filtered_text;
            }
        } catch (const std::exception& e) {
            std::cerr << "快速识别编码转换失败: " << e.what() << std::endl;
            // 保留原始文本
            result.text = filtered_text;
        }
    } else {
        // 不需要转换
        result.text = filtered_text;
    }
    
    // 修改：检查是否有output_queue，如果有则推送到output_queue，否则推送到input_queue
    if (output_queue) {
        output_queue->push(result);
        std::cout << "结果已推送到输出队列" << std::endl;
    } else if (input_queue) {
        input_queue->push(result);
        std::cout << "结果已推送到输入队列" << std::endl;
    }
    
    std::cout << "Fast recognition completed in " << rectime << "ms for " 
              << audio_length_ms << "ms audio. Text: " << result.text << std::endl;
}

// PreciseRecognizer实现
PreciseRecognizer::PreciseRecognizer(const std::string& model_path, ResultQueue* input_queue,
                                   const std::string& language, bool use_gpu, float vad_threshold,
                                   Translator* translator)
    : model_path(model_path),
      input_queue(input_queue),
      language(language),
      use_gpu(use_gpu),
      vad_threshold(vad_threshold),
      translator(translator) {
    // 在构造函数中加载模型
    struct whisper_context_params params = whisper_context_default_params();
    
    // 配置 GPU 使用
    if (use_gpu) {
        // 设置 GPU 相关参数
        params.use_gpu = true;
        params.gpu_device = 0;  // 使用第一个 GPU 设备
        
        // 根据whisper.h定义，flash_attn参数是可选的，设置为false
        params.flash_attn = false;
        
        std::cout << "精确识别启用GPU加速，设备ID: 0" << std::endl;
    } else {
        params.use_gpu = false;
        std::cout << "精确识别使用CPU模式运行" << std::endl;
    }
    
    // 加载模型
    ctx = whisper_init_from_file_with_params(model_path.c_str(), params);
    if (!ctx) {
        std::cerr << "Failed to load precise recognition model: " << model_path << std::endl;
        throw std::runtime_error("Failed to initialize precise recognition model");
    }
    
    std::cout << "Precise recognition model loaded successfully: " << model_path 
              << (use_gpu ? " (GPU enabled)" : " (CPU mode)") << std::endl;
}

PreciseRecognizer::~PreciseRecognizer() {
    if (ctx) {
        whisper_free(ctx);
    }
}

void PreciseRecognizer::start() {
    running = true;
}

void PreciseRecognizer::stop() {
    running = false;
}

void PreciseRecognizer::process_audio_batch(const std::vector<AudioBuffer>& batch) {
    if (!ctx) {
        std::cerr << "Precise recognition model not properly loaded, cannot perform recognition" << std::endl;
        return;
    }
    
    // 优化：减少日志输出
    static int batch_counter = 0;
    bool should_log = (++batch_counter % 10 == 0); // 只记录每10个批次
    
    // 设置识别参数
    whisper_full_params full_params = whisper_full_default_params(WHISPER_SAMPLING_BEAM_SEARCH);
    // 添加指令防止生成无关内容
    //full_params.initial_prompt = "请只转录实际音频内容，不要生成任何额外内容如点赞订阅语句";
    full_params.print_progress = false;
    full_params.print_special = false;
    full_params.print_realtime = false;
    full_params.print_timestamps = true;
    
    full_params.language = language.c_str();
    
    // 优化: 设置更高效的线程数，避免过度创建线程
    if (use_gpu) {
        full_params.n_threads = 2;  // GPU模式下使用较少线程
    } else {
        // 根据CPU核心数自动调整线程数
        int num_cores = std::thread::hardware_concurrency();
        full_params.n_threads = 6; // 至少使用2个线程
    }
    
    // 优化参数，提高性能
    full_params.n_max_text_ctx = 8192; // 减小上下文长度，提高处理速度
    full_params.temperature = 0.0f;
    full_params.beam_search.beam_size = 5; // 5束搜索，力求精准
    
    // 合并音频数据
    std::vector<float> combined_data;
    combined_data.reserve(batch.size() * batch[0].data.size()); // 预分配内存
    for (const auto& buffer : batch) {
        combined_data.insert(combined_data.end(), buffer.data.begin(), buffer.data.end());
    }
    
    // 计算音频长度（毫秒）
    const int sample_rate = 16000;  // 16kHz采样率
    float audio_length_ms = static_cast<float>(combined_data.size()) * 1000.0f / sample_rate;
    
    // 如果音频长度小于VAD阈值，跳过处理
    const float min_audio_length_ms = 300.0f; // 降低最小音频长度阈值
    if (audio_length_ms < min_audio_length_ms) {
        // 只有在音频长度真的很短时才输出警告
        if (should_log && audio_length_ms < 100.0f) {
            std::cerr << "Precise recognizer: Audio too short - " << audio_length_ms 
                      << " ms < " << min_audio_length_ms << " ms, skipping" << std::endl;
        }
        return;
    }
    
    if (should_log) {
        std::cout << "Precise recognizer processing audio length: " << audio_length_ms << " ms" << std::endl;
    }
    
    // 修改：如果存在翻译器，则直接启动翻译线程处理相同的音频数据
    std::thread translator_thread;
    if (translator && audio_length_ms > min_audio_length_ms) {
        translator_thread = std::thread([this, &combined_data, audio_length_ms, should_log]() {
            try {
                // 使用显式类型转换避免警告
                translator->process_audio_data(combined_data.data(), 
                                            static_cast<int>(combined_data.size()));
                if (should_log) {
                    std::cout << "Translator thread processing " << audio_length_ms << "ms audio data" << std::endl;
                }
            } catch (const std::exception& e) {
                std::cerr << "Translator thread error: " << e.what() << std::endl;
            }
        });
    }
    
    // 执行识别时使用显式类型转换
    auto recstart = std::chrono::high_resolution_clock::now();
    if (whisper_full(ctx, full_params, combined_data.data(), 
                    static_cast<int>(combined_data.size())) != 0) {
        std::cerr << "Precise recognition failed" << std::endl;
        
        // 如果识别失败但翻译线程已启动，确保它能完成
        if (translator_thread.joinable()) {
            translator_thread.join();
        }
        
        return;
    }
    
    // 获取并处理识别结果
    int num_segments = whisper_full_n_segments(ctx);
    for (int i = 0; i < num_segments; i++) {
        const char* text = whisper_full_get_segment_text(ctx, i);
        RecognitionResult result;
        
        // 过滤文本，移除特殊标记
        std::string filtered_text = text;
        std::vector<std::pair<std::string, std::string>> replace_patterns = {
            {"[音乐]", ""}, {"[掌声]", ""}, {"[笑声]", ""},
            {"[Music]", ""}, {"[Applause]", ""}, {"[Laughter]", ""},
            {"[MUSIC]", ""}, {"[APPLAUSE]", ""}, {"[LAUGHTER]", ""},
            {"*", ""}, {"\n", " "}
        };
        
        for (const auto& pattern : replace_patterns) {
            size_t pos = 0;
            while ((pos = filtered_text.find(pattern.first, pos)) != std::string::npos) {
                filtered_text.replace(pos, pattern.first.length(), pattern.second);
                pos += pattern.second.length();
            }
        }
        
        // 添加编码转换
        // 检测文本是否有编码问题
        bool needs_conversion = false;
        // 检查是否包含乱码特征
        int latin_char_count = 0;
        for (char c : filtered_text) {
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
                latin_char_count++;
            }
        }
        // 如果拉丁字母占比超过70%且文本长度足够长，很可能是编码问题
        if (latin_char_count > filtered_text.length() * 0.7 && filtered_text.length() > 10) {
            needs_conversion = true;
        }
        
        if (needs_conversion) {
            try {
                // 使用Windows API进行GBK到UTF-8的转换
                std::string utf8_text;
                
                // 1. 首先将GBK编码的字符串转换为宽字符(UTF-16)
                int wstr_size = MultiByteToWideChar(
                    936, // CP_ACP for GBK/GB2312
                    0,
                    filtered_text.c_str(),
                    -1, // 以null结尾的字符串
                    NULL,
                    0  // 获取所需大小
                );
                
                if (wstr_size > 0) {
                    std::wstring wide_text(wstr_size, 0);
                    MultiByteToWideChar(
                        936,
                        0,
                        filtered_text.c_str(),
                        -1,
                        &wide_text[0],
                        wstr_size
                    );
                    
                    // 移除末尾的null字符
                    if (!wide_text.empty() && wide_text.back() == L'\0') {
                        wide_text.pop_back();
                    }
                    
                    // 2. 然后将UTF-16转换为UTF-8
                    int utf8_size = WideCharToMultiByte(
                        CP_UTF8,
                        0,
                        wide_text.c_str(),
                        -1,
                        NULL,
                        0,
                        NULL,
                        NULL
                    );
                    
                    if (utf8_size > 0) {
                        std::string converted_text(utf8_size, 0);
                        WideCharToMultiByte(
                            CP_UTF8,
                            0,
                            wide_text.c_str(),
                            -1,
                            &converted_text[0],
                            utf8_size,
                            NULL,
                            NULL
                        );
                        
                        // 移除末尾的null字符
                        if (!converted_text.empty() && converted_text.back() == '\0') {
                            converted_text.pop_back();
                        }
                        
                        utf8_text = converted_text;
                    }
                }
                
                // 使用转换后的文本
                if (!utf8_text.empty()) {
                    result.text = utf8_text;
                    std::cout << "精确识别编码转换成功: " << utf8_text << std::endl;
                } else {
                    // 如果转换后为空，使用原始文本
                    result.text = filtered_text;
                }
            } catch (const std::exception& e) {
                std::cerr << "精确识别编码转换失败: " << e.what() << std::endl;
                // 保留原始文本
                result.text = filtered_text;
            }
        } else {
            // 不需要转换
            result.text = filtered_text;
        }
        
        // 推送结果到合适的队列
        if (output_queue) {
            output_queue->push(result);
            if (should_log) {
                std::cout << "精确识别结果已推送到输出队列: segment " << i+1 << "/" << num_segments << std::endl;
            }
        } else if (input_queue) {
            input_queue->push(result);
            if (should_log) {
                std::cout << "精确识别结果已推送到输入队列: segment " << i+1 << "/" << num_segments << std::endl;
            }
        }
    }
    
    // 等待翻译线程完成
    if (translator_thread.joinable()) {
        translator_thread.join();
        if (should_log) {
            std::cout << "Translator thread joined" << std::endl;
        }
    }
    
    auto recend = std::chrono::high_resolution_clock::now();
    auto rectime = std::chrono::duration_cast<std::chrono::milliseconds>(recend - recstart);
	std::cout << "本轮精确识别共识别" << audio_length_ms << "ms音频,用时" << rectime.count() << "ms"<<std::endl;
} 