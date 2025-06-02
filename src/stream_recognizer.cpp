#include <QApplication>
#include <QtGui/QFontDatabase>
#include <whisper_gui.h>
#include <audio_processor.h>
#include "loading_dialog.h"
#include <config_manager.h>
#include <QSettings>
#include <QMessageBox>
#include <QDir>
#include <QStandardPaths>
#include <QDebug>
#include <log_utils.h>
#include <iostream>
#include <exception>
#include <signal.h>
#include <QTimer>

// 全局变量定义
bool g_use_gpu = true;

// 信号处理函数，用于捕获崩溃
void signalHandler(int signal) {
    std::cerr << "捕获到信号: " << signal << std::endl;
    
    // 如果是非法指令错误，可能是CUDA相关问题
    if (signal == SIGILL) {
        std::cerr << "检测到非法指令错误，可能是CUDA/GPU兼容性问题" << std::endl;
        
        // 将GPU设置保存为false
        QSettings settings("StreamRecognizer", "WhisperApp");
        settings.setValue("use_gpu", false);
        settings.sync();
        
        // 显示错误消息
        QMessageBox::critical(nullptr, "GPU兼容性错误",
                            "检测到GPU兼容性问题，程序将自动切换到CPU模式。\n"
                            "请重启应用程序。");
    }
    
    // 结束程序
    exit(signal);
}

int main(int argc, char* argv[]) {
    // 设置信号处理
    signal(SIGILL, signalHandler);  // 非法指令
    signal(SIGSEGV, signalHandler); // 段错误
    signal(SIGABRT, signalHandler); // 中止信号
    
    try {
    QApplication app(argc, argv);
        
        // 设置应用程序信息
        QCoreApplication::setOrganizationName("StreamRecognizer");
        QCoreApplication::setApplicationName("WhisperApp");
    
    // 添加字体支持
    QFont font("Microsoft YaHei", 9);  // 使用微软雅黑字体
    app.setFont(font);
    
    // 加载配置
    auto& config = ConfigManager::getInstance();
    if (!config.loadConfig("config.json")) {
        QMessageBox::critical(nullptr, "Error", "Failed to load config file");
        return 1;
    }
        
        // 读取GPU设置
        QSettings settings("StreamRecognizer", "WhisperApp");
        g_use_gpu = settings.value("use_gpu", true).toBool();
    
    // 创建加载对话框
    LoadingDialog loadingDialog;
    loadingDialog.setMaximum(3);  // 3个模型需要加载
    loadingDialog.show();
    app.processEvents();  // 确保对话框显示
    
    // 先创建GUI窗口
    std::unique_ptr<WhisperGUI> gui = std::make_unique<WhisperGUI>();
    
    // 然后创建音频处理器，并传入GUI指针
    std::unique_ptr<AudioProcessor> processor = std::make_unique<AudioProcessor>(gui.get());
    
    // 预加载模型 - 从配置文件获取模型路径
    bool success = processor->preloadModels(
        [&loadingDialog](const std::string& message) {
            loadingDialog.setMessage(QString::fromStdString(message));
            loadingDialog.setProgress(loadingDialog.progress() + 1);
            QApplication::processEvents();
        }
    );
    
    if (!success) {
        QMessageBox::critical(nullptr, "Error", "Failed to load models");
        return 1;
    }
    
    // 关闭加载对话框
    loadingDialog.close();
    
    // 显示主窗口
    gui->show();
    
    // 在Qt事件循环启动后连接媒体播放器信号
    QTimer::singleShot(100, processor.get(), &AudioProcessor::connectMediaPlayerSignals);
        
        // 设置全局异常处理
        std::set_terminate([]() {
            try {
                std::cerr << "检测到未处理的异常" << std::endl;
                
                // 尝试获取当前异常
                auto currentException = std::current_exception();
                if (currentException) {
                    try {
                        std::rethrow_exception(currentException);
                    } catch (const std::exception& e) {
                        std::cerr << "异常信息: " << e.what() << std::endl;
                        
                        // 检查是否是CUDA相关错误
                        std::string error = e.what();
                        if (error.find("CUDA") != std::string::npos || 
                            error.find("cuda") != std::string::npos ||
                            error.find("GPU") != std::string::npos) {
                            
                            // 将GPU设置保存为false
                            QSettings settings("StreamRecognizer", "WhisperApp");
                            settings.setValue("use_gpu", false);
                            settings.sync();
                            
                            QMessageBox::critical(nullptr, "GPU错误",
                                                "检测到CUDA/GPU相关错误，程序将自动切换到CPU模式。\n"
                                                "错误信息: " + QString::fromStdString(e.what()) + "\n"
                                                "请重启应用程序。");
                        } else {
                            QMessageBox::critical(nullptr, "程序错误",
                                                "发生未处理的异常: " + QString::fromStdString(e.what()));
                        }
                    } catch (...) {
                        std::cerr << "未知异常类型" << std::endl;
                        QMessageBox::critical(nullptr, "未知错误", "发生未知类型的异常");
                    }
                }
            } catch (...) {
                // 最后的防御
                QMessageBox::critical(nullptr, "严重错误", "处理异常时发生错误");
            }
            
            // 终止程序
            abort();
        });
    
    // 运行Qt事件循环
    int result = app.exec();
    
    // 在程序退出前，确保正确的析构顺序
    // 先析构AudioProcessor，再析构GUI
    processor.reset();
    gui.reset();
    
    return result;
    } catch (const std::exception& e) {
        std::cerr << "主函数捕获到异常: " << e.what() << std::endl;
        
        // 检查是否是CUDA相关错误
        std::string error = e.what();
        if (error.find("CUDA") != std::string::npos || 
            error.find("cuda") != std::string::npos ||
            error.find("GPU") != std::string::npos) {
            
            // 将GPU设置保存为false
            QSettings settings("StreamRecognizer", "WhisperApp");
            settings.setValue("use_gpu", false);
            settings.sync();
            
            QMessageBox::critical(nullptr, "GPU错误",
                                "检测到CUDA/GPU相关错误，程序将自动切换到CPU模式。\n"
                                "错误信息: " + QString::fromStdString(e.what()) + "\n"
                                "请重启应用程序。");
        } else {
            QMessageBox::critical(nullptr, "启动错误",
                                "程序启动失败: " + QString::fromStdString(e.what()));
        }
        
        return 1;
    } catch (...) {
        std::cerr << "主函数捕获到未知异常" << std::endl;
        QMessageBox::critical(nullptr, "严重错误", "程序启动时发生未知错误");
        return 1;
    }
}