#include "loading_dialog.h"
#include <QApplication>
#include <QThread>
#include <QMetaObject>

LoadingDialog::LoadingDialog(QWidget* parent)
    : QDialog(parent)
{
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
}

void LoadingDialog::setMessage(const QString& message)
{
    // 确保在主线程中更新GUI
    if (QThread::currentThread() == this->thread()) {
        messageLabel->setText(message);
    } else {
        QMetaObject::invokeMethod(this, [this, message]() {
    messageLabel->setText(message);
        }, Qt::QueuedConnection);
    }
}

void LoadingDialog::setProgress(int value)
{
    // 确保在主线程中更新GUI
    if (QThread::currentThread() == this->thread()) {
        progressBar->setValue(value);
    } else {
        QMetaObject::invokeMethod(this, [this, value]() {
    progressBar->setValue(value);
        }, Qt::QueuedConnection);
    }
}

void LoadingDialog::setMaximum(int maximum)
{
    // 确保在主线程中更新GUI
    if (QThread::currentThread() == this->thread()) {
        progressBar->setMaximum(maximum);
    } else {
        QMetaObject::invokeMethod(this, [this, maximum]() {
    progressBar->setMaximum(maximum);
        }, Qt::QueuedConnection);
    }
} 