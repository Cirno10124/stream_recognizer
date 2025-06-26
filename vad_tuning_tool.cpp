#include <iostream>
#include <vector>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <iomanip>

/**
 * VAD调优诊断工具
 * 用于分析音频数据并推荐最佳VAD参数
 */
class VADTuningTool {
public:
    struct AudioStats {
        float mean_energy = 0.0f;
        float max_energy = 0.0f;
        float min_energy = 0.0f;
        float energy_variance = 0.0f;
        float zero_crossing_rate = 0.0f;
        float dynamic_range = 0.0f;
        float rms_energy = 0.0f;
        int silence_segments = 0;
        int voice_segments = 0;
        float noise_floor = 0.0f;
    };
    
    struct VADRecommendations {
        float energy_threshold = 0.04f;
        float vad_threshold = 0.04f;
        int min_voice_frames = 3;
        int voice_hold_frames = 8;
        int vad_mode = 2;
        bool adaptive_mode = true;
        float background_threshold_multiplier = 2.5f;
    };
    
    static AudioStats analyzeAudioSegment(const std::vector<float>& audio_data) {
        AudioStats stats;
        
        if (audio_data.empty()) return stats;
        
        // 1. 计算能量统计
        float sum_energy = 0.0f;
        float sum_squared = 0.0f;
        stats.max_energy = -1.0f;
        stats.min_energy = 1.0f;
        
        for (float sample : audio_data) {
            float energy = sample * sample;
            sum_energy += energy;
            sum_squared += energy * energy;
            stats.max_energy = std::max(stats.max_energy, energy);
            stats.min_energy = std::min(stats.min_energy, energy);
        }
        
        stats.mean_energy = sum_energy / audio_data.size();
        stats.rms_energy = std::sqrt(stats.mean_energy);
        stats.energy_variance = (sum_squared / audio_data.size()) - (stats.mean_energy * stats.mean_energy);
        stats.dynamic_range = stats.max_energy - stats.min_energy;
        
        // 2. 计算过零率
        int zero_crossings = 0;
        for (size_t i = 1; i < audio_data.size(); i++) {
            if ((audio_data[i] >= 0) != (audio_data[i-1] >= 0)) {
                zero_crossings++;
            }
        }
        stats.zero_crossing_rate = (float)zero_crossings / audio_data.size();
        
        // 3. 估算噪音底噪
        std::vector<float> energies;
        for (float sample : audio_data) {
            energies.push_back(sample * sample);
        }
        std::sort(energies.begin(), energies.end());
        // 取最低20%的能量作为噪音底噪估计
        size_t noise_samples = energies.size() * 0.2;
        float noise_sum = 0.0f;
        for (size_t i = 0; i < noise_samples; i++) {
            noise_sum += energies[i];
        }
        stats.noise_floor = noise_sum / noise_samples;
        
        return stats;
    }
    
    static VADRecommendations generateRecommendations(const AudioStats& stats) {
        VADRecommendations rec;
        
        std::cout << "\n🔍 音频分析结果:" << std::endl;
        std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << std::endl;
        std::cout << "📊 能量统计:" << std::endl;
        std::cout << "   平均能量: " << std::fixed << std::setprecision(6) << stats.mean_energy << std::endl;
        std::cout << "   RMS能量:  " << std::fixed << std::setprecision(6) << stats.rms_energy << std::endl;
        std::cout << "   最大能量: " << std::fixed << std::setprecision(6) << stats.max_energy << std::endl;
        std::cout << "   最小能量: " << std::fixed << std::setprecision(6) << stats.min_energy << std::endl;
        std::cout << "   动态范围: " << std::fixed << std::setprecision(6) << stats.dynamic_range << std::endl;
        std::cout << "   噪音底噪: " << std::fixed << std::setprecision(6) << stats.noise_floor << std::endl;
        std::cout << "   过零率:   " << std::fixed << std::setprecision(3) << stats.zero_crossing_rate << std::endl;
        
        // 基于分析结果生成推荐
        std::cout << "\n🎯 VAD参数推荐:" << std::endl;
        std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << std::endl;
        
        // 1. 能量阈值推荐
        if (stats.noise_floor > 0.01f) {
            rec.energy_threshold = stats.noise_floor * 3.0f;
            std::cout << "⚠️  检测到较高背景噪音!" << std::endl;
            std::cout << "   推荐能量阈值: " << std::fixed << std::setprecision(4) << rec.energy_threshold 
                      << " (噪音底噪的3倍)" << std::endl;
        } else if (stats.noise_floor > 0.005f) {
            rec.energy_threshold = stats.noise_floor * 4.0f;
            std::cout << "📢 检测到中等背景噪音" << std::endl;
            std::cout << "   推荐能量阈值: " << std::fixed << std::setprecision(4) << rec.energy_threshold 
                      << " (噪音底噪的4倍)" << std::endl;
        } else if (stats.noise_floor > 0.001f) {
            rec.energy_threshold = stats.noise_floor * 5.0f;
            std::cout << "🔇 检测到低背景噪音" << std::endl;
            std::cout << "   推荐能量阈值: " << std::fixed << std::setprecision(4) << rec.energy_threshold 
                      << " (噪音底噪的5倍)" << std::endl;
        } else {
            rec.energy_threshold = 0.008f;
            std::cout << "✨ 音频质量很好，噪音很低" << std::endl;
            std::cout << "   推荐能量阈值: " << std::fixed << std::setprecision(4) << rec.energy_threshold << std::endl;
        }
        
        // 2. VAD模式推荐
        if (stats.zero_crossing_rate > 0.3f) {
            rec.vad_mode = 3;  // 最严格模式
            std::cout << "🎵 检测到高过零率，可能有音乐或复杂音频" << std::endl;
            std::cout << "   推荐VAD模式: 3 (最严格)" << std::endl;
        } else if (stats.zero_crossing_rate > 0.15f) {
            rec.vad_mode = 2;  // 严格模式
            std::cout << "🗣️  正常语音特征" << std::endl;
            std::cout << "   推荐VAD模式: 2 (严格)" << std::endl;
        } else {
            rec.vad_mode = 1;  // 中等模式
            std::cout << "📻 低过零率，可能是单调语音" << std::endl;
            std::cout << "   推荐VAD模式: 1 (中等)" << std::endl;
        }
        
        // 3. 帧数参数推荐
        if (stats.energy_variance > 0.01f) {
            rec.min_voice_frames = 5;
            rec.voice_hold_frames = 12;
            std::cout << "🌊 能量变化较大，需要更多帧来稳定检测" << std::endl;
            std::cout << "   推荐最小语音帧: " << rec.min_voice_frames << std::endl;
            std::cout << "   推荐保持帧数: " << rec.voice_hold_frames << std::endl;
        } else {
            rec.min_voice_frames = 3;
            rec.voice_hold_frames = 8;
            std::cout << "📈 能量相对稳定" << std::endl;
            std::cout << "   推荐最小语音帧: " << rec.min_voice_frames << std::endl;
            std::cout << "   推荐保持帧数: " << rec.voice_hold_frames << std::endl;
        }
        
        // 4. 自适应模式推荐
        if (stats.noise_floor > 0.005f || stats.energy_variance > 0.005f) {
            rec.adaptive_mode = true;
            rec.background_threshold_multiplier = 3.0f;
            std::cout << "🔄 推荐启用自适应模式" << std::endl;
            std::cout << "   背景噪音倍数: " << rec.background_threshold_multiplier << std::endl;
        } else {
            rec.adaptive_mode = false;
            std::cout << "🔒 推荐使用固定阈值模式" << std::endl;
        }
        
        return rec;
    }
    
    static void printConfigRecommendations(const VADRecommendations& rec) {
        std::cout << "\n📝 建议的config.json配置:" << std::endl;
        std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << std::endl;
        std::cout << R"({
    "audio": {
        "vad_advanced": {
            "adaptive_mode": )" << (rec.adaptive_mode ? "true" : "false") << R"(,
            "energy_threshold": )" << std::fixed << std::setprecision(4) << rec.energy_threshold << R"(,
            "min_voice_frames": )" << rec.min_voice_frames << R"(,
            "mode": )" << rec.vad_mode << R"(,
            "voice_hold_frames": )" << rec.voice_hold_frames << R"(
        },
        "vad_threshold": )" << std::fixed << std::setprecision(4) << rec.energy_threshold << R"(
    }
})" << std::endl;
        
        std::cout << "\n🚀 代码中的设置方法:" << std::endl;
        std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << std::endl;
        std::cout << "// 在初始化VAD时设置这些参数:" << std::endl;
        std::cout << "vad.setEnergyThreshold(" << std::fixed << std::setprecision(4) << rec.energy_threshold << "f);" << std::endl;
        std::cout << "vad.setVADMode(" << rec.vad_mode << ");" << std::endl;
        std::cout << "vad.setMinVoiceFrames(" << rec.min_voice_frames << ");" << std::endl;
        std::cout << "vad.setVoiceHoldFrames(" << rec.voice_hold_frames << ");" << std::endl;
        std::cout << "vad.setAdaptiveMode(" << (rec.adaptive_mode ? "true" : "false") << ");" << std::endl;
    }
    
    static void analyzePotentialIssues(const AudioStats& stats) {
        std::cout << "\n⚠️  潜在问题诊断:" << std::endl;
        std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << std::endl;
        
        bool has_issues = false;
        
        if (stats.noise_floor > 0.02f) {
            std::cout << "🔊 问题1: 背景噪音过高 (噪音底噪: " << std::fixed << std::setprecision(4) << stats.noise_floor << ")" << std::endl;
            std::cout << "   解决方案: 启用自适应模式，提高能量阈值" << std::endl;
            has_issues = true;
        }
        
        if (stats.zero_crossing_rate > 0.4f) {
            std::cout << "🎵 问题2: 过零率过高 (ZCR: " << std::fixed << std::setprecision(3) << stats.zero_crossing_rate << ")" << std::endl;
            std::cout << "   可能原因: 背景音乐、音效或高频噪音" << std::endl;
            std::cout << "   解决方案: 使用最严格VAD模式(3)，增加最小语音帧数" << std::endl;
            has_issues = true;
        }
        
        if (stats.dynamic_range < 0.001f) {
            std::cout << "📉 问题3: 动态范围过小 (范围: " << std::fixed << std::setprecision(6) << stats.dynamic_range << ")" << std::endl;
            std::cout << "   可能原因: 音频增益过低或压缩过度" << std::endl;
            std::cout << "   解决方案: 检查音频输入增益设置" << std::endl;
            has_issues = true;
        }
        
        if (stats.mean_energy > 0.1f) {
            std::cout << "📢 问题4: 平均能量过高 (能量: " << std::fixed << std::setprecision(4) << stats.mean_energy << ")" << std::endl;
            std::cout << "   可能原因: 音频增益过高或持续背景噪音" << std::endl;
            std::cout << "   解决方案: 降低音频输入增益，提高VAD阈值" << std::endl;
            has_issues = true;
        }
        
        if (!has_issues) {
            std::cout << "✅ 未检测到明显问题，音频质量良好" << std::endl;
        }
    }
};

// 使用示例
void demonstrateVADTuning() {
    std::cout << "🎛️  VAD调优诊断工具" << std::endl;
 