#pragma once

#include <string>
#include <iostream>
#include <QDebug>
#include <QMetaObject>
#include <QString>
#include <functional>
#include <whisper_gui.h>


// 前向声明，避免循环引用
//class WhisperGUI;

// 修改为使用std::cout的日志宏，避免中文乱码问题
#define LOG_INFO(msg) std::cout << "[INFO] " << msg << std::endl
#define LOG_ERROR(msg) std::cerr << "[ERROR] " << msg << std::endl
#define LOG_WARNING(msg) std::cout << "[WARNING] " << msg << std::endl
#define LOG_DEBUG(msg) std::cout << "[DEBUG] " << msg << std::endl

// 声明为内联函数，避免多重定义
inline void logMessage(WhisperGUI* gui, const std::string& message, bool isError = false) {
    if (gui) {
        // 将std::string转换为QString
        QString qMessage = QString::fromStdString(message);
        
        try {
            if (isError) {
                // 使用Lambda表达式形式，更现代的方式调用槽函数
                QMetaObject::invokeMethod(gui, "appendErrorMessage", Qt::QueuedConnection, 
                                         Q_ARG(QString, qMessage));
            } else {
                // 同样使用Lambda表达式形式调用
                QMetaObject::invokeMethod(gui, "appendLogMessage", Qt::QueuedConnection, 
                                         Q_ARG(QString, qMessage));
            }
        } catch (const std::exception& e) {
            std::cerr << "GUI log output failed: " << e.what() << std::endl;
            std::cerr << "Original message: " << message << std::endl;
        }
    }
    
    // 始终输出到控制台，无论GUI是否可用
    if (isError) {
        std::cerr << "ERROR: " << message << std::endl;
    } else {
        std::cout << "INFO: " << message << std::endl;
    }
}