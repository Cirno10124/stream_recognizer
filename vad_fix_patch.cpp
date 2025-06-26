// VAD误检测修复补丁
// 在voice_activity_detector.cpp中添加以下改进

// 1. 改进的isRealVoice方法 - 更严格的语音验证
bool VoiceActivityDetector::isRealVoice(const std::vector<float>& audio_frame, bool webrtc_result, float energy) {
    if (!webrtc_result) return false;
    
    // 1. 提高能量阈值 - 从0.04改为0.015
    if (energy < 0.015f) return false;
    
    // 2. 更严格的动态范围检查
    float max_val = *std::max_element(audio_frame.begin(), audio_frame.end());
    float min_val = *std::min_element(audio_frame.begin(), audio_frame.end());
    float dynamic_range = max_val - min_val;
    if (dynamic_range < 0.008f) return false; // 提高动态范围要求
    
    // 3. 更严格的过零率检查
    int zero_crossings = 0;
    for (size_t i = 1; i < audio_frame.size(); i++) {
        if ((audio_frame[i] >= 0) != (audio_frame[i-1] >= 0)) {
            zero_crossings++;
        }
    }
    float zcr = (float)zero_crossings / audio_frame.size();
    // 更严格的过零率范围：排除高频噪音和音乐
    if (zcr > 0.35f || zcr < 0.01f) return false;
    
    // 4. 频谱能量分布检查（简化版）
    float low_freq_energy = 0.0f;
    float high_freq_energy = 0.0f;
    size_t half_size = audio_frame.size() / 2;
    
    for (size_t i = 0; i < half_size; i++) {
        low_freq_energy += audio_frame[i] * audio_frame[i];
    }
    for (size_t i = half_size; i < audio_frame.size(); i++) {
        high_freq_energy += audio_frame[i] * audio_frame[i];
    }
    
    // 语音通常低频能量占主导
    float freq_ratio = low_freq_energy / (high_freq_energy + 1e-6f);
    if (freq_ratio < 0.8f) return false; // 高频过多，可能是噪音
    
    // 5. 如果启用自适应模式，检查是否明显高于背景噪音
    if (adaptive_mode && background_frames_count > 10) {
        if (energy < background_energy * 3.5f) return false; // 提高倍数要求
    }
    
    return true;
}

// 2. 改进的检测逻辑 - 更严格的投票机制
bool VoiceActivityDetector::detect(const std::vector<float>& audio_buffer, int sample_rate) {
    // ... 现有代码 ...
    
    // 改进的投票决策：70%静音帧才认为整段为静音（更严格）
    bool has_voice = false;
    if (total_frames > 0) {
        float voice_ratio = static_cast<float>(voice_frames) / total_frames;
        float silence_ratio = 1.0f - voice_ratio;
        
        // 如果静音帧比例 >= 70%，则认为整段为静音
        has_voice = (silence_ratio < 0.7f);
        
        // 额外检查：如果语音帧很少但能量很高，可能是噪音
        if (has_voice && voice_ratio < 0.4f && current_energy > 0.05f) {
            has_voice = false; // 可能是持续噪音
        }
    }
    
    // ... 其余代码保持不变 ...
}

// 3. 添加混合VAD的改进逻辑
bool VoiceActivityDetector::detectWithHybridVAD(const std::vector<float>& audio_buffer) {
    if (vad_type_ != VADType::Hybrid) {
        return detect(audio_buffer, 16000);
    }
    
    // WebRTC VAD结果
    bool webrtc_result = detect(audio_buffer, 16000);
    
    // Silero VAD结果
    float silero_probability = getSileroVADProbability(audio_buffer);
    bool silero_result = silero_probability > 0.6f; // 提高Silero阈值
    
    // 能量门控
    float energy = calculateEnergy(audio_buffer);
    bool energy_gate = energy > 0.015f;
    
    // 混合决策：要求所有条件都满足
    bool final_result = webrtc_result && silero_result && energy_gate;
    
    // 调试输出
    static int debug_counter = 0;
    if (++debug_counter % 50 == 0) { // 每50帧输出一次
        std::cout << "[VAD] 混合检测 - WebRTC:" << webrtc_result 
                  << ", Silero:" << silero_result << "(" << std::fixed << std::setprecision(2) << silero_probability << ")"
                  << ", 能量:" << energy_gate << "(" << std::fixed << std::setprecision(4) << energy << ")"
                  << ", 结果:" << final_result << std::endl;
    }
    
    return final_result;
}

// 4. 构造函数中的参数调整
VoiceActivityDetector::VoiceActivityDetector(float threshold, QObject* parent, 
                                           VADType vad_type, const std::string& silero_model_path)
    : QObject(parent)
    , threshold(threshold)
    // ... 其他初始化 ...
    , min_voice_frames(6)         // 增加到6帧（120ms）
    , voice_hold_frames(10)       // 增加到10帧（200ms）
    , vad_mode(3)                 // 使用最严格模式
    , required_silence_frames(15) // 增加到300ms
    , energy_threshold(0.015f)    // 提高能量阈值
{
    // ... 现有初始化代码 ...
} 