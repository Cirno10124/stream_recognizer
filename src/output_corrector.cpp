#include "output_corrector.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QEventLoop>
#include <QTimer>
#include <QUrl>
#include <QDebug>
#include <thread>
#include <chrono>

OutputCorrector::OutputCorrector(QObject* parent)
    : QObject(parent), network_manager_(new QNetworkAccessManager(this)) {
    
    // 设置默认配置
    config_.server_url = "http://localhost:8000";
    config_.model_name = "deepseek-coder-7b-instruct-v1.5";
    config_.temperature = 0.1;
    config_.max_tokens = 512;
    config_.stream_mode = false;
}

OutputCorrector::~OutputCorrector() {
    if (network_manager_) {
        network_manager_->deleteLater();
    }
}

void OutputCorrector::setConfig(const CorrectionConfig& config) {
    config_ = config;
}

std::string OutputCorrector::buildPrompt(const std::string& input_text) {
    std::string prompt = R"(你是一个专业的语音识别输出矫正助手。请对以下语音识别结果进行矫正：

任务要求：
1. 纠正明显的语音识别错误（如同音字错误）
2. 补充缺失的标点符号
3. 优化语句的通顺性和可读性
4. 保持原意不变，不要添加原文没有的信息
5. 如果是英文，请纠正语法和拼写错误
6. 输出格式要整洁规范

原始文本：)";
    
    prompt += input_text;
    prompt += R"(

请输出矫正后的文本：)";
    
    return prompt;
}

QJsonObject OutputCorrector::buildRequest(const std::string& input_text) {
    QJsonObject request;
    request["model"] = QString::fromStdString(config_.model_name);
    request["temperature"] = config_.temperature;
    request["max_tokens"] = config_.max_tokens;
    request["stream"] = config_.stream_mode;
    
    // 构建消息数组
    QJsonArray messages;
    QJsonObject message;
    message["role"] = "user";
    message["content"] = QString::fromStdString(buildPrompt(input_text));
    messages.append(message);
    
    request["messages"] = messages;
    
    return request;
}

std::string OutputCorrector::parseResponse(const QJsonDocument& response) {
    QJsonObject root = response.object();
    
    if (root.contains("error")) {
        qWarning() << "DeepSeek API Error:" << root["error"].toObject()["message"].toString();
        return "";
    }
    
    if (root.contains("choices")) {
        QJsonArray choices = root["choices"].toArray();
        if (!choices.isEmpty()) {
            QJsonObject firstChoice = choices[0].toObject();
            QJsonObject message = firstChoice["message"].toObject();
            QString content = message["content"].toString();
            
            // 简单处理，提取矫正后的文本
            QString result = content.trimmed();
            
            // 如果包含"矫正后的文本："等提示，尝试提取
            if (result.contains("矫正后的文本：")) {
                int startPos = result.indexOf("矫正后的文本：") + 7;
                result = result.mid(startPos).trimmed();
            } else if (result.contains("Output:")) {
                int startPos = result.indexOf("Output:") + 7;
                result = result.mid(startPos).trimmed();
            }
            
            return result.toStdString();
        }
    }
    
    return "";
}

bool OutputCorrector::isServiceAvailable() {
    try {
        QUrl url(QString::fromStdString(config_.server_url + "/v1/models"));
        QNetworkRequest request(url);
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        
        QNetworkReply* reply = network_manager_->get(request);
        
        QEventLoop loop;
        QTimer::singleShot(5000, &loop, &QEventLoop::quit); // 5秒超时
        connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        
        bool success = (reply->error() == QNetworkReply::NoError);
        reply->deleteLater();
        
        return success;
    } catch (...) {
        return false;
    }
}

std::string OutputCorrector::correctText(const std::string& input_text) {
    if (input_text.empty()) {
        return input_text;
    }
    
    try {
        // 构建请求
        QJsonObject requestObj = buildRequest(input_text);
        QJsonDocument requestDoc(requestObj);
        
        // 发送请求
        QUrl url(QString::fromStdString(config_.server_url + "/v1/chat/completions"));
        QNetworkRequest request(url);
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        
        QNetworkReply* reply = network_manager_->post(request, requestDoc.toJson());
        
        // 等待响应
        QEventLoop loop;
        QTimer::singleShot(30000, &loop, &QEventLoop::quit); // 30秒超时
        connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        
        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "Network error:" << reply->errorString();
            reply->deleteLater();
            return input_text; // 如果出错，返回原文本
        }
        
        // 解析响应
        QByteArray responseData = reply->readAll();
        reply->deleteLater();
        
        QJsonParseError parseError;
        QJsonDocument responseDoc = QJsonDocument::fromJson(responseData, &parseError);
        
        if (parseError.error != QJsonParseError::NoError) {
            qWarning() << "JSON parse error:" << parseError.errorString();
            return input_text;
        }
        
        std::string corrected = parseResponse(responseDoc);
        return corrected.empty() ? input_text : corrected;
        
    } catch (const std::exception& e) {
        qWarning() << "Exception in correctText:" << e.what();
        return input_text;
    }
}

std::future<std::string> OutputCorrector::correctTextAsync(const std::string& input_text) {
    return std::async(std::launch::async, [this, input_text]() {
        return correctText(input_text);
    });
}

std::vector<std::string> OutputCorrector::correctBatch(const std::vector<std::string>& input_texts) {
    std::vector<std::string> results;
    results.reserve(input_texts.size());
    
    // 并发处理多个文本
    std::vector<std::future<std::string>> futures;
    
    for (const auto& text : input_texts) {
        futures.push_back(correctTextAsync(text));
    }
    
    // 收集结果
    for (auto& future : futures) {
        try {
            results.push_back(future.get());
        } catch (...) {
            results.push_back(""); // 出错时返回空字符串
        }
    }
    
    return results;
}

void OutputCorrector::onCorrectionFinished() {
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    
    if (reply->error() == QNetworkReply::NoError) {
        QByteArray responseData = reply->readAll();
        QJsonDocument responseDoc = QJsonDocument::fromJson(responseData);
        std::string corrected = parseResponse(responseDoc);
        
        emit correctionCompleted(QString::fromStdString(corrected));
    } else {
        emit correctionFailed(reply->errorString());
    }
    
    reply->deleteLater();
}

void OutputCorrector::onCorrectionError() {
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (reply) {
        emit correctionFailed(reply->errorString());
        reply->deleteLater();
    }
}

// 逐行矫正功能实现
std::string OutputCorrector::correctLineByLine(const std::string& current_line) {
    if (current_line.empty()) {
        return current_line;
    }
    
    try {
        // 构建上下文（包含之前的行）
        std::string previous_context;
        if (!line_history_.empty()) {
            for (const auto& line : line_history_) {
                if (!previous_context.empty()) {
                    previous_context += "\n";
                }
                previous_context += line;
            }
        }
        
        // 构建逐行矫正请求
        QJsonObject requestObj = buildLineByLineRequest(current_line, previous_context);
        QJsonDocument requestDoc(requestObj);
        
        // 发送请求
        QUrl url(QString::fromStdString(config_.server_url + "/v1/chat/completions"));
        QNetworkRequest request(url);
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        
        QNetworkReply* reply = network_manager_->post(request, requestDoc.toJson());
        
        // 等待响应
        QEventLoop loop;
        QTimer::singleShot(30000, &loop, &QEventLoop::quit); // 30秒超时
        connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        
        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "Network error in line-by-line correction:" << reply->errorString();
            reply->deleteLater();
            return current_line; // 如果出错，返回原文本
        }
        
        // 解析响应
        QByteArray responseData = reply->readAll();
        reply->deleteLater();
        
        QJsonParseError parseError;
        QJsonDocument responseDoc = QJsonDocument::fromJson(responseData, &parseError);
        
        if (parseError.error != QJsonParseError::NoError) {
            qWarning() << "JSON parse error in line-by-line correction:" << parseError.errorString();
            return current_line;
        }
        
        std::string corrected = parseResponse(responseDoc);
        std::string result = corrected.empty() ? current_line : corrected;
        
        // 更新历史记录
        line_history_.push_back(result);
        if (line_history_.size() > max_history_lines_) {
            line_history_.pop_front();
        }
        
        return result;
        
    } catch (const std::exception& e) {
        qWarning() << "Exception in correctLineByLine:" << e.what();
        return current_line;
    }
}

std::future<std::string> OutputCorrector::correctLineByLineAsync(const std::string& current_line) {
    return std::async(std::launch::async, [this, current_line]() {
        return correctLineByLine(current_line);
    });
}

void OutputCorrector::resetLineHistory() {
    line_history_.clear();
}

QJsonObject OutputCorrector::buildLineByLineRequest(const std::string& current_line, const std::string& previous_context) {
    QJsonObject request;
    request["model"] = QString::fromStdString(config_.model_name);
    request["temperature"] = config_.temperature;
    request["max_tokens"] = config_.max_tokens;
    request["stream"] = config_.stream_mode;
    
    // 构建消息数组
    QJsonArray messages;
    QJsonObject message;
    message["role"] = "user";
    message["content"] = QString::fromStdString(buildLineByLinePrompt(current_line, previous_context));
    messages.append(message);
    
    request["messages"] = messages;
    
    return request;
}

std::string OutputCorrector::buildLineByLinePrompt(const std::string& current_line, const std::string& previous_context) {
    std::string prompt = R"(你是一个专业的语音识别输出矫正助手。请对当前行的语音识别结果进行矫正，需要考虑上下文的连贯性。

任务要求：
1. 纠正当前行中明显的语音识别错误（如同音字错误）
2. 根据上下文调整当前行的内容，确保语义连贯
3. 补充缺失的标点符号
4. 优化语句的通顺性和可读性
5. 保持原意不变，不要添加原文没有的信息
6. 如果是英文，请纠正语法和拼写错误
7. 只输出矫正后的当前行内容，不要输出上下文
8. 如果当前行与上一行内容重复，请去重或合并处理

)";

    if (!previous_context.empty()) {
        prompt += "上下文：\n" + previous_context + "\n\n";
    }
    
    prompt += "当前行：" + current_line + "\n\n";
    prompt += "请输出矫正后的当前行：";
    
    return prompt;
} 