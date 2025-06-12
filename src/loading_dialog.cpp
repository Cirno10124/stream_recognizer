#include "loading_dialog.h"
#include "memory_serializer.h"
#include <QApplication>
#include <QThread>
#include <QMetaObject>
#include "log_utils.h"

LoadingDialog::LoadingDialog(QWidget* parent)
    : QDialog(parent)
{
    LOG_INFO("开始创建LoadingDialog");
    
    // 使用串行分配器确保所有UI组件在主线程中创建
    MemorySerializer::getInstance().executeSerial([this]() {
    setWindowTitle("Loading Models");
    setFixedSize(400, 150);
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    
    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(20);
    layout->setContentsMargins(20, 20, 20, 20);
    
    messageLabel = new QLabel("Loading models, please wait...", this);
    messageLabel->setAlignment(Qt::AlignCenter);
    messageLabel->setStyleSheet("font-size: 14px; color: #333;");
    
    progressBar = new QProgressBar(this);
    progressBar->setStyleSheet(
        "QProgressBar {"
        "    border: 2px solid #ccc;"
        "    border-radius: 5px;"
        "    text-align: center;"
        "    background-color: #f0f0f0;"
        "}"
        "QProgressBar::chunk {"
        "    background-color: #4CAF50;"
        "    border-radius: 3px;"
        "}"
    );
    
    layout->addWidget(messageLabel);
    layout->addWidget(progressBar);
    
    // 设置窗口样式
    setStyleSheet(
        "QDialog {"
        "    background-color: white;"
        "    border: 1px solid #ccc;"
        "    border-radius: 10px;"
        "}"
    );
        
        LOG_INFO("LoadingDialog UI组件创建完成");
    });
    
    LOG_INFO("LoadingDialog构造函数完成");
}

LoadingDialog::~LoadingDialog() {
    LOG_INFO("开始销毁LoadingDialog");
    
    // 使用串行分配器确保安全销毁UI组件
    MemorySerializer::getInstance().executeSerial([this]() {
        // Qt的父子关系会自动处理子组件的销毁
        // 这里只需要确保指针安全
        messageLabel = nullptr;
        progressBar = nullptr;
        LOG_INFO("LoadingDialog UI组件引用已清空");
    });
    
    LOG_INFO("LoadingDialog析构函数完成");
}

void LoadingDialog::setMessage(const QString& message)
{
    // 使用串行分配器确保线程安全的GUI更新
    MemorySerializer::getInstance().executeSerial([this, message]() {
        if (messageLabel) {
        messageLabel->setText(message);
            LOG_INFO("LoadingDialog消息更新: " + message.toStdString());
    }
    });
}

void LoadingDialog::setProgress(int value)
{
    // 使用串行分配器确保线程安全的GUI更新
    MemorySerializer::getInstance().executeSerial([this, value]() {
        if (progressBar) {
        progressBar->setValue(value);
            LOG_INFO("LoadingDialog进度更新: " + std::to_string(value));
    }
    });
}

void LoadingDialog::setMaximum(int maximum)
{
    // 使用串行分配器确保线程安全的GUI更新
    MemorySerializer::getInstance().executeSerial([this, maximum]() {
        if (progressBar) {
        progressBar->setMaximum(maximum);
            LOG_INFO("LoadingDialog最大值设置: " + std::to_string(maximum));
    }
    });
} 