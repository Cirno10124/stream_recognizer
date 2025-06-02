#include "audio_processor.h"
#include <whisper.h>
#include <thread>
#include <chrono>
#include <iostream>
#include <sstream>
#include "log_utils.h"

// 辅助函数：将文本转换为PCM格式的音频数据（模拟）
std::vector<float> text_to_pcm(const std::string& text) {
    // 这里我们创建一个简单的音频信号来表示文本
    // 实际应用中应该使用TTS引擎将文本转换为真实的音频
    std::vector<float> pcm;
    const int sample_rate = 16000;  // whisper期望16kHz采样率
    const float duration = 0.1f;    // 每个字符的持续时间（秒）
    const float frequency = 440.0f;  // 基频
    
    size_t num_samples = static_cast<size_t>(sample_rate * duration * text.length());
    pcm.resize(num_samples);
    
    for (size_t i = 0; i < num_samples; ++i) {
        float t = static_cast<float>(i) / sample_rate;
        // 生成一个简单的正弦波
        pcm[i] = 0.5f * std::sin(2.0f * M_PI * frequency * t);
    }
    
    return pcm;
}

Translator::Translator(const std::string& model_path, ResultQueue* input_queue,
                      ResultQueue* output_queue, const std::string& target_lang,
                      bool dual_lang)
    : model_path(model_path),
      input_queue(input_queue),
      output_queue(output_queue),
      target_language(target_lang),
      dual_language(dual_lang) {
    
    // 在构造函数中加载模型
    struct whisper_context_params params = whisper_context_default_params();
    ctx = whisper_init_from_file_with_params(model_path.c_str(), params);
    if (!ctx) {
        LOG_ERROR("无法加载翻译模型: " + model_path);
        throw std::runtime_error("翻译模型加载失败");
    }
    LOG_INFO("翻译模型加载成功: " + model_path);
}

Translator::~Translator() {
    if (ctx) {
        whisper_free(ctx);
        ctx = nullptr;
    }
}

void Translator::start() {
    running = true;
}

void Translator::stop() {
    running = false;
}

void Translator::process_results() {
    if (!ctx) {
        LOG_ERROR("翻译模型未加载，无法处理翻译");
        return;
    }
    
    // 检查输入队列是否已初始化
    if (!input_queue) {
        LOG_ERROR("翻译器输入队列未初始化");
        return;
    }
    
    // 检查输出队列是否已初始化
    if (!output_queue) {
        LOG_ERROR("翻译器输出队列未初始化");
        return;
    }
    
    LOG_INFO("翻译处理线程启动");
    
    // 超时和错误恢复相关变量
    const int POP_TIMEOUT_MS = 100; // 队列弹出操作的超时时间(毫秒)
    const int MAX_CONSECUTIVE_FAILURES = 10; // 允许的最大连续失败次数
    int consecutive_failures = 0; // 当前连续失败次数
    
    while (running) {
        RecognitionResult result;
        bool timeout_expired = false;
        
        try {
            // 使用超时方式获取结果，避免永久阻塞
            auto timeout_point = std::chrono::steady_clock::now() + std::chrono::milliseconds(POP_TIMEOUT_MS);
            
            // 使用自定义的wait_until条件变量，避免无限等待
            std::unique_lock<std::mutex> lock(input_queue->getMutex());
            bool condition_met = input_queue->getCondition().wait_until(lock, timeout_point, [this, &result]() {
                // 如果队列非空或已终止，处理数据
                if (!input_queue->isEmpty() || input_queue->is_terminated()) {
                    // 尝试获取一个结果但不阻塞
                    if (!input_queue->isEmpty()) {
                        result = std::move(input_queue->front());
                        input_queue->pop_internal();
                        return true;
                    }
                    return true; // 队列已终止，也返回true以退出等待
                }
                return false; // 继续等待
            });
            
            // 检查是否因超时退出
            timeout_expired = !condition_met;
            
            // 检查队列是否终止
            if (input_queue->is_terminated()) {
                LOG_INFO("翻译输入队列已终止，结束翻译处理");
                break;
            }
            
            // 超时无数据，继续下一轮
            if (timeout_expired || result.text.empty()) {
                // 重置连续失败计数
                consecutive_failures = 0;
                continue;
            }
        } catch (const std::exception& e) {
            LOG_ERROR("翻译器获取输入数据异常: " + std::string(e.what()));
            
            // 增加失败计数
            consecutive_failures++;
            
            // 如果连续失败次数过多，暂停一段时间再重试
            if (consecutive_failures >= MAX_CONSECUTIVE_FAILURES) {
                LOG_WARNING("翻译器连续失败次数过多，暂停重试");
                std::this_thread::sleep_for(std::chrono::seconds(1));
                consecutive_failures = 0;
            }
            
            continue;
        }
        
        // 成功获取结果，重置错误计数
        consecutive_failures = 0;
        
        // 创建新的结果
        RecognitionResult processed_result;
        processed_result.timestamp = result.timestamp;
        
        try {
            // 检查是否需要翻译
            if (!target_language.empty() && target_language != "none") {
                LOG_INFO("接收到待翻译文本: " + result.text);
                
                // 直接使用原始文本
                processed_result.text = result.text;
                
                // 如果启用了翻译模式，执行翻译
                if (target_language != "none") {
                    auto start_time = std::chrono::high_resolution_clock::now();
                    
                    // 设置翻译参数 - 使用beam search获得更好的翻译质量
                    whisper_full_params params = whisper_full_default_params(WHISPER_SAMPLING_BEAM_SEARCH);
                    
                    // 配置翻译相关参数
                    params.print_progress = false;
                    params.print_special = false;
                    params.print_realtime = false;
                    params.print_timestamps = false;
                    
                    // 启用翻译模式，设置目标语言
                    params.translate = true;
                    params.language = target_language.c_str();
                    
                    // 设置性能参数
                    params.n_threads = 4;
                    params.beam_search.beam_size = 5;  // 更大的beam size提高翻译质量
                    
                    // 对原文进行翻译处理
                    // 由于Whisper需要音频输入，我们使用嵌入式提示方式处理文本翻译
                    // 创建一个小的音频样本（实际上我们只是需要提供输入来触发Whisper的处理）
                    std::vector<float> dummy_audio(16000, 0.0f);  // 1秒静音
                    
                    // 设置初始提示（包含原文），引导模型进行翻译而非识别
                    std::string prompt = "Translate to " + target_language + ": " + result.text;
                    params.initial_prompt = prompt.c_str();
                    
                    // 执行whisper处理
                    if (whisper_full(ctx, params, dummy_audio.data(), dummy_audio.size()) != 0) {
                        throw std::runtime_error("翻译执行失败");
                    }
                    
                    // 收集翻译结果
                    std::stringstream translated_text;
                    int n_segments = whisper_full_n_segments(ctx);
                    
                    for (int i = 0; i < n_segments; ++i) {
                        const char* segment_text = whisper_full_get_segment_text(ctx, i);
                        if (segment_text) {
                            translated_text << segment_text;
                        }
                    }
                    
                    // 检查翻译结果是否有效
                    std::string translation = translated_text.str();
                    if (!translation.empty()) {
                        // 根据dual_language决定输出格式
                        if (dual_language) {
                            processed_result.text = result.text + "\n" + translation;
                            LOG_INFO("生成双语输出");
                        } else {
                            processed_result.text = translation;
                            LOG_INFO("生成单语翻译");
                        }
                    } else {
                        LOG_WARNING("翻译结果为空，使用原文");
                        // 翻译失败，使用原文
                        processed_result.text = result.text;
                    }
                    
                    auto end_time = std::chrono::high_resolution_clock::now();
                    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
                    LOG_INFO("翻译完成，耗时: " + std::to_string(duration) + "ms");
                }
            } else {
                // 不需要翻译，直接传递原始文本
                processed_result.text = result.text;
                LOG_INFO("无需翻译，直接传递原始文本");
            }
            
            // 发送结果
            if (!processed_result.text.empty()) {
                try {
                    output_queue->push(processed_result);
                } catch (const std::exception& e) {
                    LOG_ERROR("推送翻译结果到输出队列异常: " + std::string(e.what()));
                }
            }
            
        } catch (const std::exception& e) {
            LOG_ERROR("翻译处理错误: " + std::string(e.what()));
            // 发生错误时，返回原始文本
            processed_result.text = result.text;
            try {
                output_queue->push(processed_result);
            } catch (const std::exception& e) {
                LOG_ERROR("推送原始文本到输出队列异常: " + std::string(e.what()));
            }
        }
    }
    
    LOG_INFO("翻译处理线程结束");
}

void Translator::process_audio_data(const float* audio_data, size_t audio_data_size) {
    if (!ctx) {
        LOG_ERROR("翻译模型未加载，无法处理翻译");
        return;
    }
    
    // 检查输出队列是否已初始化
    if (!output_queue) {
        LOG_ERROR("翻译器输出队列未初始化");
        return;
    }
    
    // 检查翻译功能是否启用
    if (target_language.empty() || target_language == "none") {
        LOG_INFO("翻译功能未启用，跳过音频翻译处理");
        return;
    }
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        LOG_INFO("开始处理音频数据进行直接翻译，数据大小: " + std::to_string(audio_data_size));
        
        // 设置翻译参数 - 使用beam search获得更好的翻译质量
        whisper_full_params params = whisper_full_default_params(WHISPER_SAMPLING_BEAM_SEARCH);
        
        // 配置翻译相关参数
        params.print_progress = false;
        params.print_special = false;
        params.print_realtime = false;
        params.print_timestamps = false;
        
        // 启用翻译模式，设置目标语言
        params.translate = true;
        params.language = target_language.c_str();
        
        // 设置性能参数
        params.n_threads = 4;
        params.beam_search.beam_size = 5;  // 更大的beam size提高翻译质量
        
        // 执行whisper处理 - 直接使用原始音频数据
        if (whisper_full(ctx, params, audio_data, audio_data_size) != 0) {
            throw std::runtime_error("翻译执行失败");
        }
        
        // 收集翻译结果
        std::stringstream translated_text;
        int n_segments = whisper_full_n_segments(ctx);
        
        LOG_INFO("翻译完成，获取到 " + std::to_string(n_segments) + " 个段落");
        
        for (int i = 0; i < n_segments; ++i) {
            const char* segment_text = whisper_full_get_segment_text(ctx, i);
            if (segment_text) {
                translated_text << segment_text;
            }
        }
        
        std::string translation = translated_text.str();
        if (!translation.empty()) {
            // 创建新的结果对象
            RecognitionResult result;
            result.timestamp = std::chrono::system_clock::now();
            
            // 设置结果文本 - 对于直接翻译我们只有翻译结果而没有原文
            // 在这种情况下，即使dual_language为true，我们也只输出翻译结果
            result.text = translation;
            
            // 将结果推送到输出队列
            output_queue->push(result);
            
            LOG_INFO("翻译结果已推送到输出队列，长度: " + std::to_string(translation.length()));
        } else {
            LOG_WARNING("直接音频翻译未产生任何结果");
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
        LOG_INFO("音频直接翻译完成，耗时: " + std::to_string(duration) + "ms");
        
    } catch (const std::exception& e) {
        LOG_ERROR("音频直接翻译处理错误: " + std::string(e.what()));
    }
} 