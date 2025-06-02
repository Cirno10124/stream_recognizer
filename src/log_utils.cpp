#include "log_utils.h"
#include <whisper_gui.h>
#include <QMetaObject>
#include <iostream>

// 注释掉或删除重复的实现，因为已在头文件中定义为内联函数
/* 
void logMessage(WhisperGUI* gui, const std::string& message, bool isError) {
    // 已在头文件中定义为内联函数
    // 此处实现已被移除以避免重复定义错误
} 
*/

// 如果需要其他与日志相关的函数，可以在这里添加 