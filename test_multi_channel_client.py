#!/usr/bin/env python3
"""
多路识别客户端测试脚本
用于演示如何使用新的多路识别功能
"""

import sys
import time
import random
import threading
from PyQt5.QtWidgets import (QApplication, QMainWindow, QVBoxLayout, QHBoxLayout, 
                           QWidget, QPushButton, QLabel, QSpinBox, QCheckBox, 
                           QTextEdit, QFileDialog, QMessageBox, QGroupBox)
from PyQt5.QtCore import Qt, QTimer, pyqtSignal, QObject
from PyQt5.QtGui import QColor, QTextCharFormat

class MultiChannelSimulator(QObject):
    """模拟多路识别处理器的行为"""
    
    # 信号定义
    taskCompleted = pyqtSignal(str, int, str, QColor)  # task_id, channel_id, result, color
    taskError = pyqtSignal(str, int, str)              # task_id, channel_id, error
    channelStatusChanged = pyqtSignal(int, str)        # channel_id, status
    allChannelsBusy = pyqtSignal()
    channelAvailable = pyqtSignal(int)                 # channel_id
    
    def __init__(self):
        super().__init__()
        self.channels = {}
        self.task_counter = 0
        self.colors = [
            QColor(85, 170, 85),    # 绿色
            QColor(85, 170, 255),   # 蓝色
            QColor(255, 170, 85),   # 橙色
            QColor(255, 85, 170),   # 粉红色
            QColor(170, 85, 255),   # 紫色
            QColor(85, 255, 170),   # 青绿色
            QColor(255, 255, 85),   # 黄色
            QColor(170, 170, 170),  # 灰色
            QColor(255, 85, 85),    # 红色
            QColor(85, 255, 255)    # 天蓝色
        ]
        
    def initialize(self, channel_count):
        """初始化指定数量的通道"""
        self.channels = {}
        for i in range(channel_count):
            self.channels[i] = {
                'status': 'idle',
                'color': self.colors[i % len(self.colors)],
                'current_task': None
            }
        return True
        
    def submit_task(self, audio_file, language="zh", use_gpu=False):
        """提交识别任务"""
        # 查找空闲通道
        available_channel = None
        for channel_id, channel_info in self.channels.items():
            if channel_info['status'] == 'idle':
                available_channel = channel_id
                break
                
        if available_channel is None:
            self.allChannelsBusy.emit()
            return None
            
        # 生成任务ID
        self.task_counter += 1
        task_id = f"TASK_{self.task_counter:04d}"
        
        # 标记通道为忙碌
        self.channels[available_channel]['status'] = 'processing'
        self.channels[available_channel]['current_task'] = task_id
        self.channelStatusChanged.emit(available_channel, 'processing')
        
        # 模拟异步处理
        timer = QTimer()
        timer.timeout.connect(lambda: self._complete_task(task_id, available_channel, audio_file))
        timer.setSingleShot(True)
        
        # 随机处理时间 (1-5秒)
        processing_time = random.randint(1000, 5000)
        timer.start(processing_time)
        
        return task_id
        
    def _complete_task(self, task_id, channel_id, audio_file):
        """完成任务处理"""
        if channel_id not in self.channels:
            return
            
        # 模拟识别结果
        sample_results = [
            "这是语音识别的测试结果",
            "多路识别功能正常工作",
            "Hello, this is a test recognition result",
            "系统运行稳定，识别准确率良好",
            "通道处理能力测试成功",
            "语音转文字功能正常",
            "多线程处理效果良好",
            "Real-time speech recognition works well"
        ]
        
        # 随机选择结果或错误
        if random.random() < 0.1:  # 10%的错误率
            error_msg = f"Processing failed for {audio_file}"
            self.taskError.emit(task_id, channel_id, error_msg)
        else:
            result = random.choice(sample_results)
            color = self.channels[channel_id]['color']
            self.taskCompleted.emit(task_id, channel_id, result, color)
            
        # 恢复通道状态
        self.channels[channel_id]['status'] = 'idle'
        self.channels[channel_id]['current_task'] = None
        self.channelStatusChanged.emit(channel_id, 'idle')
        self.channelAvailable.emit(channel_id)
        
    def get_channel_stats(self):
        """获取通道统计信息"""
        available = sum(1 for ch in self.channels.values() if ch['status'] == 'idle')
        busy = sum(1 for ch in self.channels.values() if ch['status'] == 'processing')
        return available, busy
        
    def clear_all_tasks(self):
        """清理所有任务"""
        for channel_info in self.channels.values():
            channel_info['status'] = 'idle'
            channel_info['current_task'] = None
            
    def pause_all_channels(self):
        """暂停所有通道"""
        for channel_id, channel_info in self.channels.items():
            if channel_info['status'] != 'processing':
                channel_info['status'] = 'paused'
                self.channelStatusChanged.emit(channel_id, 'paused')
                
    def resume_all_channels(self):
        """恢复所有通道"""
        for channel_id, channel_info in self.channels.items():
            if channel_info['status'] == 'paused':
                channel_info['status'] = 'idle'
                self.channelStatusChanged.emit(channel_id, 'idle')


class MultiChannelTestGUI(QMainWindow):
    """多路识别测试GUI"""
    
    def __init__(self):
        super().__init__()
        self.simulator = MultiChannelSimulator()
        self.setup_ui()
        self.setup_connections()
        
    def setup_ui(self):
        """设置UI界面"""
        self.setWindowTitle("多路识别客户端测试")
        self.setGeometry(100, 100, 1000, 700)
        
        central_widget = QWidget()
        self.setCentralWidget(central_widget)
        main_layout = QVBoxLayout(central_widget)
        
        # 控制区域
        control_group = QGroupBox("控制面板")
        control_layout = QHBoxLayout(control_group)
        
        # 启用多路识别
        self.enable_checkbox = QCheckBox("启用多路识别")
        control_layout.addWidget(self.enable_checkbox)
        
        # 通道数设置
        control_layout.addWidget(QLabel("通道数:"))
        self.channel_spinbox = QSpinBox()
        self.channel_spinbox.setRange(1, 10)
        self.channel_spinbox.setValue(4)
        self.channel_spinbox.setEnabled(False)
        control_layout.addWidget(self.channel_spinbox)
        
        # 控制按钮
        self.submit_button = QPushButton("提交任务")
        self.submit_button.setEnabled(False)
        control_layout.addWidget(self.submit_button)
        
        self.clear_button = QPushButton("清理任务")
        self.clear_button.setEnabled(False)
        control_layout.addWidget(self.clear_button)
        
        self.pause_button = QPushButton("暂停所有")
        self.pause_button.setEnabled(False)
        control_layout.addWidget(self.pause_button)
        
        self.resume_button = QPushButton("恢复所有")
        self.resume_button.setEnabled(False)
        control_layout.addWidget(self.resume_button)
        
        control_layout.addStretch()
        main_layout.addWidget(control_group)
        
        # 状态显示区域
        status_group = QGroupBox("状态信息")
        status_layout = QHBoxLayout(status_group)
        
        self.status_label = QLabel("多路识别未启用")
        status_layout.addWidget(self.status_label)
        
        # 自动提交测试
        self.auto_submit_checkbox = QCheckBox("自动提交测试任务")
        status_layout.addWidget(self.auto_submit_checkbox)
        
        status_layout.addStretch()
        main_layout.addWidget(status_group)
        
        # 输出区域
        output_layout = QHBoxLayout()
        
        # 单路输出
        single_group = QGroupBox("单路识别输出")
        single_layout = QVBoxLayout(single_group)
        self.single_output = QTextEdit()
        self.single_output.setReadOnly(True)
        single_layout.addWidget(self.single_output)
        output_layout.addWidget(single_group, 1)
        
        # 多路输出
        multi_group = QGroupBox("多路识别输出")
        multi_layout = QVBoxLayout(multi_group)
        self.multi_output = QTextEdit()
        self.multi_output.setReadOnly(True)
        self.multi_output.setVisible(False)
        multi_layout.addWidget(self.multi_output)
        output_layout.addWidget(multi_group, 1)
        
        # 日志输出
        log_group = QGroupBox("系统日志")
        log_layout = QVBoxLayout(log_group)
        self.log_output = QTextEdit()
        self.log_output.setReadOnly(True)
        log_layout.addWidget(self.log_output)
        output_layout.addWidget(log_group, 1)
        
        main_layout.addLayout(output_layout)
        
        # 自动提交定时器
        self.auto_timer = QTimer()
        self.auto_timer.timeout.connect(self.auto_submit_task)
        
    def setup_connections(self):
        """设置信号连接"""
        # UI控件连接
        self.enable_checkbox.toggled.connect(self.on_multi_channel_toggled)
        self.channel_spinbox.valueChanged.connect(self.on_channel_count_changed)
        self.submit_button.clicked.connect(self.on_submit_task)
        self.clear_button.clicked.connect(self.on_clear_tasks)
        self.pause_button.clicked.connect(self.on_pause_channels)
        self.resume_button.clicked.connect(self.on_resume_channels)
        self.auto_submit_checkbox.toggled.connect(self.on_auto_submit_toggled)
        
        # 模拟器信号连接
        self.simulator.taskCompleted.connect(self.on_task_completed)
        self.simulator.taskError.connect(self.on_task_error)
        self.simulator.channelStatusChanged.connect(self.on_channel_status_changed)
        self.simulator.allChannelsBusy.connect(self.on_all_channels_busy)
        self.simulator.channelAvailable.connect(self.on_channel_available)
        
    def log_message(self, message):
        """记录日志消息"""
        timestamp = time.strftime("%H:%M:%S")
        formatted_message = f"[{timestamp}] {message}"
        self.log_output.append(formatted_message)
        
    def on_multi_channel_toggled(self, enabled):
        """多路识别模式切换"""
        self.channel_spinbox.setEnabled(enabled)
        self.submit_button.setEnabled(enabled)
        self.clear_button.setEnabled(enabled)
        self.pause_button.setEnabled(enabled)
        self.resume_button.setEnabled(enabled)
        
        if enabled:
            self.single_output.setVisible(False)
            self.multi_output.setVisible(True)
            
            # 初始化模拟器
            if self.simulator.initialize(self.channel_spinbox.value()):
                self.status_label.setText(f"多路识别已启用 ({self.channel_spinbox.value()} 通道)")
                self.log_message("多路识别模式已启用")
            else:
                self.status_label.setText("多路识别初始化失败")
                self.log_message("多路识别初始化失败")
                self.enable_checkbox.setChecked(False)
        else:
            self.single_output.setVisible(True)
            self.multi_output.setVisible(False)
            self.status_label.setText("多路识别未启用")
            self.log_message("多路识别模式已禁用")
            
            # 停止自动提交
            if self.auto_timer.isActive():
                self.auto_timer.stop()
                self.auto_submit_checkbox.setChecked(False)
                
    def on_channel_count_changed(self, count):
        """通道数量变更"""
        if self.enable_checkbox.isChecked():
            if self.simulator.initialize(count):
                self.status_label.setText(f"多路识别已更新 ({count} 通道)")
                self.log_message(f"通道数已更新为 {count}")
            else:
                self.status_label.setText("通道数更新失败")
                self.log_message("通道数更新失败")
                
    def on_submit_task(self):
        """提交识别任务"""
        if not self.enable_checkbox.isChecked():
            return
            
        # 模拟音频文件
        test_files = [
            "test_audio_1.wav",
            "test_audio_2.mp3", 
            "test_stream.m3u8",
            "sample_recording.wav",
            "demo_speech.flac"
        ]
        
        audio_file = random.choice(test_files)
        task_id = self.simulator.submit_task(audio_file)
        
        if task_id:
            self.log_message(f"已提交任务: {task_id} (文件: {audio_file})")
            available, busy = self.simulator.get_channel_stats()
            self.status_label.setText(f"通道状态: {available} 空闲, {busy} 忙碌")
        else:
            self.log_message("提交任务失败 - 所有通道都在忙碌")
            
    def on_clear_tasks(self):
        """清理所有任务"""
        self.simulator.clear_all_tasks()
        self.log_message("已清理所有任务")
        available, busy = self.simulator.get_channel_stats()
        self.status_label.setText(f"任务已清理 ({available} 通道可用)")
        
    def on_pause_channels(self):
        """暂停所有通道"""
        self.simulator.pause_all_channels()
        self.log_message("已暂停所有通道")
        self.status_label.setText("所有通道已暂停")
        
    def on_resume_channels(self):
        """恢复所有通道"""
        self.simulator.resume_all_channels()
        self.log_message("已恢复所有通道")
        available, busy = self.simulator.get_channel_stats()
        self.status_label.setText(f"通道已恢复: {available} 空闲, {busy} 忙碌")
        
    def on_auto_submit_toggled(self, enabled):
        """自动提交任务切换"""
        if enabled and self.enable_checkbox.isChecked():
            self.auto_timer.start(2000)  # 每2秒提交一个任务
            self.log_message("自动提交任务已启用")
        else:
            self.auto_timer.stop()
            self.log_message("自动提交任务已禁用")
            
    def auto_submit_task(self):
        """自动提交任务"""
        if self.enable_checkbox.isChecked():
            self.on_submit_task()
            
    def on_task_completed(self, task_id, channel_id, result, color):
        """任务完成处理"""
        # 在多路输出中显示结果
        cursor = self.multi_output.textCursor()
        cursor.movePosition(cursor.End)
        
        # 设置颜色格式
        format = QTextCharFormat()
        format.setForeground(color)
        cursor.setCharFormat(format)
        
        timestamp = time.strftime("%H:%M:%S")
        formatted_result = f"[通道{channel_id + 1}] {timestamp} {result}\n"
        cursor.insertText(formatted_result)
        
        # 滚动到底部
        scrollbar = self.multi_output.verticalScrollBar()
        scrollbar.setValue(scrollbar.maximum())
        
        self.log_message(f"通道 {channel_id + 1} 完成任务 {task_id}")
        
        # 更新状态
        available, busy = self.simulator.get_channel_stats()
        self.status_label.setText(f"通道状态: {available} 空闲, {busy} 忙碌")
        
    def on_task_error(self, task_id, channel_id, error):
        """任务错误处理"""
        cursor = self.multi_output.textCursor()
        cursor.movePosition(cursor.End)
        
        # 设置错误颜色格式
        format = QTextCharFormat()
        format.setForeground(QColor(255, 0, 0))  # 红色
        cursor.setCharFormat(format)
        
        timestamp = time.strftime("%H:%M:%S")
        formatted_error = f"[通道{channel_id + 1} 错误] {timestamp} {error}\n"
        cursor.insertText(formatted_error)
        
        # 滚动到底部
        scrollbar = self.multi_output.verticalScrollBar()
        scrollbar.setValue(scrollbar.maximum())
        
        self.log_message(f"通道 {channel_id + 1} 任务 {task_id} 出错: {error}")
        
    def on_channel_status_changed(self, channel_id, status):
        """通道状态变更"""
        status_text = {
            'idle': '空闲',
            'processing': '处理中',
            'paused': '暂停',
            'error': '错误'
        }.get(status, status)
        
        self.log_message(f"通道 {channel_id + 1} 状态: {status_text}")
        
    def on_all_channels_busy(self):
        """所有通道都忙碌"""
        self.status_label.setText("所有通道都在忙碌 - 考虑增加更多通道")
        self.log_message("所有通道当前都在忙碌")
        
    def on_channel_available(self, channel_id):
        """通道变为可用"""
        available, busy = self.simulator.get_channel_stats()
        self.status_label.setText(f"通道 {channel_id + 1} 可用 ({available} 总可用, {busy} 忙碌)")


def main():
    app = QApplication(sys.argv)
    app.setStyle('Fusion')  # 使用现代样式
    
    window = MultiChannelTestGUI()
    window.show()
    
    # 显示欢迎信息
    window.log_message("=== 多路识别客户端测试 ===")
    window.log_message("1. 勾选'启用多路识别'开始测试")
    window.log_message("2. 调整通道数并观察性能变化")
    window.log_message("3. 使用'自动提交测试任务'观察并发处理效果")
    window.log_message("4. 不同通道的输出会用不同颜色显示")
    
    sys.exit(app.exec_())


if __name__ == "__main__":
    main() 