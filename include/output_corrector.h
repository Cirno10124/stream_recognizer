#pragma once

#include <string>
#include <future>
#include <deque>
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>

class OutputCorrector : public QObject {
    Q_OBJECT

public:
    struct CorrectionConfig {
        std::string server_url = "http://localhost:8000";
        std::string model_name = "deepseek-coder-7b-instruct-v1.5";
        float temperature = 0.1;
        int max_tokens = 512;
        bool stream_mode = false;
    };

    explicit OutputCorrector(QObject* parent = nullptr);
    ~OutputCorrector();

    // 设置配置
    void setConfig(const CorrectionConfig& config);
    
    // 异步矫正文本
    std::future<std::string> correctTextAsync(const std::string& input_text);
    
    // 同步矫正文本
    std::string correctText(const std::string& input_text);
    
    // 批量矫正
    std::vector<std::string> correctBatch(const std::vector<std::string>& input_texts);
    
    // 逐行矫正功能 - 新增方法
    std::string correctLineByLine(const std::string& current_line);
    std::future<std::string> correctLineByLineAsync(const std::string& current_line);
    void resetLineHistory(); // 重置行历史记录
    
    // 检查服务是否可用
    bool isServiceAvailable();

public slots:
    void onCorrectionFinished();
    void onCorrectionError();

signals:
    void correctionCompleted(const QString& corrected_text);
    void correctionFailed(const QString& error_message);

private:
    CorrectionConfig config_;
    QNetworkAccessManager* network_manager_;
    
    // 逐行矫正的历史记录
    std::deque<std::string> line_history_;
    static const size_t max_history_lines_ = 3; // 保持最近3行的历史
    
    // 构建请求
    QJsonObject buildRequest(const std::string& input_text);
    
    // 构建逐行矫正请求
    QJsonObject buildLineByLineRequest(const std::string& current_line, const std::string& previous_context);
    
    // 解析响应
    std::string parseResponse(const QJsonDocument& response);
    
    // 构建提示词
    std::string buildPrompt(const std::string& input_text);
    
    // 构建逐行矫正提示词
    std::string buildLineByLinePrompt(const std::string& current_line, const std::string& previous_context);
}; 