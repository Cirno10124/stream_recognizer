#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
字幕WER计算工具 - GUI版本
功能：
1. 视频播放器
2. 字幕提取
3. WER计算
4. 结果可视化
"""

import sys
import os
import json
import tempfile
import subprocess
from pathlib import Path
from typing import List, Tuple, Optional

from PyQt5.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QPushButton, QLabel, QTextEdit, QFileDialog, QMessageBox,
    QSplitter, QGroupBox, QLineEdit, QProgressBar, QTableWidget,
    QTableWidgetItem, QTabWidget, QSlider, QFrame, QGridLayout,
    QComboBox, QSpinBox, QDoubleSpinBox, QCheckBox, QListWidget
)
from PyQt5.QtCore import (
    Qt, QThread, pyqtSignal, QTimer, QUrl, QSize
)
from PyQt5.QtGui import QFont, QPixmap, QPalette, QIcon
from PyQt5.QtMultimedia import QMediaPlayer, QMediaContent
from PyQt5.QtMultimediaWidgets import QVideoWidget

# 导入原有的核心功能类
from subtitle_wer_tool import SubtitleExtractor, SubtitleParser, WERCalculator
from video_ocr_extractor import VideoOCRExtractor

# 导入改进的OCR提取器
try:
    from improved_paddleocr_extractor import PaddleOCRExtractor
    from paddleocr_v3_stable import PaddleOCRConfig
    IMPROVED_OCR_AVAILABLE = True
    print("✅ 参数化PaddleOCR提取器可用")
except ImportError:
    IMPROVED_OCR_AVAILABLE = False
    print("❌ 参数化PaddleOCR提取器不可用")


class MediaPlayerWidget(QWidget):
    """媒体播放器组件"""
    
    def __init__(self):
        super().__init__()
        self.media_player = QMediaPlayer()
        self.video_widget = QVideoWidget()
        self.slider_being_dragged = False
        self.setup_ui()
        self.setup_connections()
        
    def setup_ui(self):
        """设置UI"""
        layout = QVBoxLayout()
        
        # 视频显示区域
        self.video_widget.setMinimumSize(640, 360)
        layout.addWidget(self.video_widget)
        
        # 控制栏
        control_layout = QHBoxLayout()
        
        # 播放/暂停按钮
        self.play_button = QPushButton("播放")
        self.play_button.setMaximumWidth(80)
        control_layout.addWidget(self.play_button)
        
        # 停止按钮
        self.stop_button = QPushButton("停止")
        self.stop_button.setMaximumWidth(80)
        control_layout.addWidget(self.stop_button)
        
        # 进度条
        self.position_slider = QSlider(Qt.Horizontal)
        self.position_slider.setMinimum(0)
        self.position_slider.setMaximum(100)
        control_layout.addWidget(self.position_slider)
        
        # 时间标签
        self.time_label = QLabel("00:00 / 00:00")
        self.time_label.setMinimumWidth(100)
        control_layout.addWidget(self.time_label)
        
        # 音量控制
        volume_label = QLabel("音量:")
        control_layout.addWidget(volume_label)
        
        self.volume_slider = QSlider(Qt.Horizontal)
        self.volume_slider.setMinimum(0)
        self.volume_slider.setMaximum(100)
        self.volume_slider.setValue(50)
        self.volume_slider.setMaximumWidth(100)
        control_layout.addWidget(self.volume_slider)
        
        layout.addLayout(control_layout)
        self.setLayout(layout)
        
    def setup_connections(self):
        """设置信号连接"""
        self.media_player.setVideoOutput(self.video_widget)
        
        # 按钮连接
        self.play_button.clicked.connect(self.toggle_playback)
        self.stop_button.clicked.connect(self.stop)
        
        # 滑块连接
        self.position_slider.sliderPressed.connect(self.slider_pressed)
        self.position_slider.sliderReleased.connect(self.slider_released)
        self.volume_slider.valueChanged.connect(self.set_volume)
        
        # 媒体播放器状态连接
        self.media_player.stateChanged.connect(self.on_state_changed)
        self.media_player.positionChanged.connect(self.on_position_changed)
        self.media_player.durationChanged.connect(self.on_duration_changed)
        
    def load_media(self, file_path: str):
        """加载媒体文件"""
        if os.path.exists(file_path):
            media_content = QMediaContent(QUrl.fromLocalFile(file_path))
            self.media_player.setMedia(media_content)
            self.play_button.setText("播放")
            
    def toggle_playback(self):
        """切换播放/暂停"""
        if self.media_player.state() == QMediaPlayer.PlayingState:
            self.media_player.pause()
        else:
            self.media_player.play()
            
    def stop(self):
        """停止播放"""
        self.media_player.stop()
        
    def set_volume(self, volume):
        """设置音量"""
        self.media_player.setVolume(volume)
        
    def slider_pressed(self):
        """进度滑块被按下"""
        self.slider_being_dragged = True
        
    def slider_released(self):
        """进度滑块被释放"""
        self.slider_being_dragged = False
        position = self.position_slider.value()
        duration = self.media_player.duration()
        if duration > 0:
            new_position = int((position / 100.0) * duration)
            self.media_player.setPosition(new_position)
            
    def on_state_changed(self, state):
        """播放状态改变"""
        if state == QMediaPlayer.PlayingState:
            self.play_button.setText("暂停")
        else:
            self.play_button.setText("播放")
            
    def on_position_changed(self, position):
        """播放位置改变"""
        if not self.slider_being_dragged:
            duration = self.media_player.duration()
            if duration > 0:
                self.position_slider.setValue(int((position / duration) * 100))
                
        # 更新时间显示
        current_time = self.format_time(position)
        total_time = self.format_time(self.media_player.duration())
        self.time_label.setText(f"{current_time} / {total_time}")
        
    def on_duration_changed(self, duration):
        """媒体时长改变"""
        self.position_slider.setMaximum(100)
        
    def format_time(self, ms):
        """格式化时间显示"""
        seconds = ms // 1000
        minutes = seconds // 60
        seconds = seconds % 60
        return f"{minutes:02d}:{seconds:02d}"


class SubtitleExtractionThread(QThread):
    """字幕提取线程"""
    
    progress = pyqtSignal(str)
    finished = pyqtSignal(list)
    error = pyqtSignal(str)
    
    def __init__(self, video_path: str, output_dir: str = None, ocr_settings: dict = None):
        super().__init__()
        self.video_path = video_path
        self.output_dir = output_dir
        self.ocr_settings = ocr_settings or {
            'sample_interval': 1.0,
            'confidence_threshold': 0.5,
            'languages': ['ch_sim', 'en'],
            'subtitle_region': None
        }
        
    def run(self):
        """运行字幕提取"""
        try:
            self.progress.emit("开始提取字幕...")
            
            # 首先尝试使用高级提取器
            subtitle_files = []
            try:
                from advanced_subtitle_extractor import AdvancedSubtitleExtractor
                self.progress.emit("使用高级字幕提取器...")
                advanced_extractor = AdvancedSubtitleExtractor()
                subtitle_files = advanced_extractor.extract_subtitles(self.video_path, self.output_dir)
                
                if subtitle_files:
                    self.progress.emit(f"高级提取器成功提取 {len(subtitle_files)} 个字幕文件")
                    self.finished.emit(subtitle_files)
                    return
            except ImportError:
                self.progress.emit("高级提取器不可用，使用基础方法")
            except Exception as e:
                self.progress.emit(f"高级提取器失败: {e}，使用基础方法")
            
            # 回退到基础提取器
            self.progress.emit("使用基础字幕提取器...")
            extractor = SubtitleExtractor()
            
            if self.output_dir:
                output_path = os.path.join(self.output_dir, Path(self.video_path).stem)
                subtitle_files = extractor.extract_subtitles(self.video_path, output_path)
            else:
                subtitle_files = extractor.extract_subtitles(self.video_path)
                
            if subtitle_files:
                self.progress.emit(f"基础提取器成功提取 {len(subtitle_files)} 个内嵌字幕文件")
                self.finished.emit(subtitle_files)
            else:
                self.progress.emit("未找到内嵌字幕，尝试使用OCR识别硬字幕...")
                # 使用OCR提取硬字幕
                ocr_files = self._extract_with_ocr()
                if ocr_files:
                    self.progress.emit(f"OCR成功识别 {len(ocr_files)} 个字幕")
                    self.finished.emit(ocr_files)
                else:
                    self.progress.emit("OCR未识别到字幕内容")
                    self.finished.emit([])
                
        except Exception as e:
            self.error.emit(f"提取字幕时发生错误: {str(e)}")
    
    def _extract_with_ocr(self):
        """使用参数化PaddleOCR提取硬字幕"""
        try:
            self.progress.emit("初始化PaddleOCR引擎...")
            
            if not IMPROVED_OCR_AVAILABLE:
                self.progress.emit("参数化PaddleOCR不可用，请检查安装")
                return []
            
            # 获取OCR设置
            ocr_settings = self.ocr_settings
            
            # 创建参数化PaddleOCR提取器
            try:
                extractor = PaddleOCRExtractor(
                    preset=ocr_settings.get('preset', 'subtitle'),
                    custom_params=ocr_settings.get('custom_params', {}),
                    use_gpu=ocr_settings.get('use_gpu', True)
                )
                
                self.progress.emit(f"使用预设配置: {ocr_settings.get('preset', 'subtitle')}")
            except Exception as e:
                self.progress.emit(f"PaddleOCR初始化失败: {e}")
                return []
            
            # 生成输出文件路径
            video_name = Path(self.video_path).stem
            if self.output_dir:
                output_path = os.path.join(self.output_dir, f"{video_name}_paddleocr.srt")
            else:
                import tempfile
                temp_dir = tempfile.mkdtemp()
                output_path = os.path.join(temp_dir, f"{video_name}_paddleocr.srt")
            
            self.progress.emit("开始PaddleOCR识别...")
            
            # 进度回调函数
            def progress_callback(message):
                self.progress.emit(message)
            
            # 执行字幕提取
            subtitles = extractor.extract_subtitles_from_video(
                video_path=self.video_path,
                output_path=output_path,
                sample_interval=ocr_settings.get('sample_interval', 2.0),
                subtitle_region=ocr_settings.get('subtitle_region'),
                confidence_threshold=ocr_settings.get('confidence_threshold', 0.2),
                progress_callback=progress_callback
            )
            
            if subtitles and os.path.exists(output_path):
                self.progress.emit(f"成功提取{len(subtitles)}条字幕")
                return [output_path]
            else:
                self.progress.emit("未识别到字幕内容")
                return []
                
        except Exception as e:
            self.progress.emit(f"PaddleOCR识别失败: {str(e)}")
            import traceback
            traceback.print_exc()
            return []


class WERCalculationThread(QThread):
    """WER计算线程"""
    
    progress = pyqtSignal(str)
    finished = pyqtSignal(float, dict)
    error = pyqtSignal(str)
    
    def __init__(self, subtitle_text: str, recognition_text: str):
        super().__init__()
        self.subtitle_text = subtitle_text
        self.recognition_text = recognition_text
        
    def run(self):
        """运行WER计算"""
        try:
            self.progress.emit("开始计算WER...")
            calculator = WERCalculator()
            wer, stats = calculator.calculate_wer(
                self.subtitle_text, 
                self.recognition_text
            )
            self.progress.emit("WER计算完成")
            self.finished.emit(wer, stats)
            
        except Exception as e:
            self.error.emit(f"计算WER时发生错误: {str(e)}")


class SubtitleWERMainWindow(QMainWindow):
    """主窗口"""
    
    def __init__(self):
        super().__init__()
        self.current_video_path = ""
        self.current_subtitle_files = []
        self.setup_ui()
        self.setup_connections()
        
    def setup_ui(self):
        """设置UI"""
        self.setWindowTitle("字幕WER计算工具 v2.0")
        self.setMinimumSize(1200, 800)
        
        # 创建中心窗口部件
        central_widget = QWidget()
        self.setCentralWidget(central_widget)
        
        # 主布局
        main_layout = QHBoxLayout(central_widget)
        
        # 创建分割器
        splitter = QSplitter(Qt.Horizontal)
        main_layout.addWidget(splitter)
        
        # 左侧面板 - 媒体播放器
        left_panel = self.create_media_panel()
        splitter.addWidget(left_panel)
        
        # 右侧面板 - 控制和结果
        right_panel = self.create_control_panel()
        splitter.addWidget(right_panel)
        
        # 设置分割器比例
        splitter.setSizes([600, 600])
        
        # 状态栏
        self.statusBar().showMessage("准备就绪")
        
    def create_media_panel(self):
        """创建媒体面板"""
        panel = QGroupBox("媒体播放器")
        layout = QVBoxLayout()
        
        # 文件选择
        file_layout = QHBoxLayout()
        self.file_path_edit = QLineEdit()
        self.file_path_edit.setPlaceholderText("选择视频文件...")
        self.file_path_edit.setReadOnly(True)
        file_layout.addWidget(self.file_path_edit)
        
        self.browse_button = QPushButton("浏览")
        self.browse_button.setMaximumWidth(80)
        file_layout.addWidget(self.browse_button)
        
        layout.addLayout(file_layout)
        
        # 媒体播放器
        self.media_player = MediaPlayerWidget()
        layout.addWidget(self.media_player)
        
        panel.setLayout(layout)
        return panel
        
    def create_control_panel(self):
        """创建控制面板"""
        panel = QWidget()
        layout = QVBoxLayout()
        
        # 创建标签页
        tabs = QTabWidget()
        
        # 字幕提取标签页
        extraction_tab = self.create_extraction_tab()
        tabs.addTab(extraction_tab, "字幕提取")
        
        # WER计算标签页
        wer_tab = self.create_wer_tab()
        tabs.addTab(wer_tab, "WER计算")
        
        # 结果标签页
        results_tab = self.create_results_tab()
        tabs.addTab(results_tab, "结果分析")
        
        layout.addWidget(tabs)
        panel.setLayout(layout)
        return panel
        
    def create_extraction_tab(self):
        """创建字幕提取标签页"""
        tab = QWidget()
        layout = QVBoxLayout()
        
        # 提取控制
        control_group = QGroupBox("字幕提取控制")
        control_layout = QVBoxLayout()
        
        # 输出目录选择
        output_layout = QHBoxLayout()
        output_layout.addWidget(QLabel("输出目录:"))
        self.output_dir_edit = QLineEdit()
        self.output_dir_edit.setPlaceholderText("默认为临时目录")
        output_layout.addWidget(self.output_dir_edit)
        
        self.output_browse_button = QPushButton("浏览")
        self.output_browse_button.setMaximumWidth(80)
        output_layout.addWidget(self.output_browse_button)
        control_layout.addLayout(output_layout)
        
        # PaddleOCR配置分组
        ocr_group = QGroupBox("PaddleOCR 3.0.0 配置")
        ocr_layout = QGridLayout()
        
        # 预设配置选择
        ocr_layout.addWidget(QLabel("预设配置:"), 0, 0)
        self.ocr_preset_combo = QComboBox()
        self.ocr_preset_combo.addItems([
            "subtitle (字幕专用)",
            "fast (快速模式)",
            "accurate (精确模式)", 
            "balanced (平衡模式)"
        ])
        self.ocr_preset_combo.setCurrentText("subtitle (字幕专用)")
        ocr_layout.addWidget(self.ocr_preset_combo, 0, 1, 1, 2)
        
        # 采样间隔
        ocr_layout.addWidget(QLabel("采样间隔 (秒):"), 1, 0)
        self.ocr_interval_spin = QDoubleSpinBox()
        self.ocr_interval_spin.setRange(0.5, 10.0)
        self.ocr_interval_spin.setValue(2.0)
        self.ocr_interval_spin.setSingleStep(0.5)
        ocr_layout.addWidget(self.ocr_interval_spin, 1, 1)
        
        # 置信度阈值
        ocr_layout.addWidget(QLabel("置信度阈值:"), 1, 2)
        self.ocr_confidence_spin = QDoubleSpinBox()
        self.ocr_confidence_spin.setRange(0.0, 1.0)
        self.ocr_confidence_spin.setValue(0.2)
        self.ocr_confidence_spin.setSingleStep(0.1)
        ocr_layout.addWidget(self.ocr_confidence_spin, 1, 3)
        
        # GPU加速
        self.ocr_gpu_checkbox = QCheckBox("使用GPU加速")
        self.ocr_gpu_checkbox.setChecked(True)
        ocr_layout.addWidget(self.ocr_gpu_checkbox, 2, 0, 1, 2)
        
        # 高级参数按钮
        self.advanced_params_button = QPushButton("高级参数设置")
        self.advanced_params_button.setEnabled(IMPROVED_OCR_AVAILABLE)
        ocr_layout.addWidget(self.advanced_params_button, 2, 2, 1, 2)
        
        ocr_group.setLayout(ocr_layout)
        control_layout.addWidget(ocr_group)
        
        # 提取按钮
        button_layout = QHBoxLayout()
        button_layout.addStretch()
        
        self.extract_button = QPushButton("提取字幕")
        self.extract_button.setMinimumHeight(40)
        self.extract_button.setStyleSheet("font-size: 14px; font-weight: bold;")
        button_layout.addWidget(self.extract_button)
        
        button_layout.addStretch()
        control_layout.addLayout(button_layout)
        
        control_group.setLayout(control_layout)
        layout.addWidget(control_group)
        
        # 字幕文件列表
        list_group = QGroupBox("提取的字幕文件")
        list_layout = QVBoxLayout()
        
        self.subtitle_list = QListWidget()
        self.subtitle_list.setMaximumHeight(150)
        list_layout.addWidget(self.subtitle_list)
        
        list_group.setLayout(list_layout)
        layout.addWidget(list_group)
        
        # 处理日志
        log_group = QGroupBox("处理日志")
        log_layout = QVBoxLayout()
        
        self.process_log = QTextEdit()
        self.process_log.setMaximumHeight(200)
        self.process_log.setReadOnly(True)
        log_layout.addWidget(self.process_log)
        
        log_group.setLayout(log_layout)
        layout.addWidget(log_group)
        
        tab.setLayout(layout)
        return tab
        
    def create_wer_tab(self):
        """创建WER计算标签页"""
        tab = QWidget()
        layout = QVBoxLayout()
        
        # 输入文本
        input_group = QGroupBox("文本输入")
        input_layout = QVBoxLayout()
        
        # 字幕文本
        input_layout.addWidget(QLabel("参考文本（字幕）:"))
        self.subtitle_text_edit = QTextEdit()
        self.subtitle_text_edit.setMaximumHeight(100)
        self.subtitle_text_edit.setPlaceholderText("从字幕文件加载或直接输入...")
        input_layout.addWidget(self.subtitle_text_edit)
        
        # 识别结果文本
        input_layout.addWidget(QLabel("识别结果文本:"))
        self.recognition_text_edit = QTextEdit()
        self.recognition_text_edit.setMaximumHeight(100)
        self.recognition_text_edit.setPlaceholderText("输入识别结果文本...")
        input_layout.addWidget(self.recognition_text_edit)
        
        # 文件加载按钮
        file_buttons_layout = QHBoxLayout()
        self.load_subtitle_button = QPushButton("从字幕文件加载")
        self.load_recognition_button = QPushButton("从文件加载识别结果")
        file_buttons_layout.addWidget(self.load_subtitle_button)
        file_buttons_layout.addWidget(self.load_recognition_button)
        input_layout.addLayout(file_buttons_layout)
        
        input_group.setLayout(input_layout)
        layout.addWidget(input_group)
        
        # 计算控制
        calc_group = QGroupBox("WER计算")
        calc_layout = QVBoxLayout()
        
        self.calculate_button = QPushButton("计算WER")
        self.calculate_button.setMinimumHeight(40)
        calc_layout.addWidget(self.calculate_button)
        
        calc_group.setLayout(calc_layout)
        layout.addWidget(calc_group)
        
        layout.addStretch()
        tab.setLayout(layout)
        return tab
        
    def create_results_tab(self):
        """创建结果标签页"""
        tab = QWidget()
        layout = QVBoxLayout()
        
        # WER结果显示
        wer_group = QGroupBox("WER计算结果")
        wer_layout = QGridLayout()
        
        # WER值
        wer_layout.addWidget(QLabel("WER:"), 0, 0)
        self.wer_value_label = QLabel("未计算")
        self.wer_value_label.setStyleSheet("font-size: 16px; font-weight: bold; color: blue;")
        wer_layout.addWidget(self.wer_value_label, 0, 1)
        
        # 详细统计
        wer_layout.addWidget(QLabel("总词数:"), 1, 0)
        self.total_words_label = QLabel("0")
        wer_layout.addWidget(self.total_words_label, 1, 1)
        
        wer_layout.addWidget(QLabel("正确词数:"), 2, 0)
        self.correct_words_label = QLabel("0")
        wer_layout.addWidget(self.correct_words_label, 2, 1)
        
        wer_layout.addWidget(QLabel("替换:"), 3, 0)
        self.substitutions_label = QLabel("0")
        wer_layout.addWidget(self.substitutions_label, 3, 1)
        
        wer_layout.addWidget(QLabel("删除:"), 4, 0)
        self.deletions_label = QLabel("0")
        wer_layout.addWidget(self.deletions_label, 4, 1)
        
        wer_layout.addWidget(QLabel("插入:"), 5, 0)
        self.insertions_label = QLabel("0")
        wer_layout.addWidget(self.insertions_label, 5, 1)
        
        wer_group.setLayout(wer_layout)
        layout.addWidget(wer_group)
        
        # 详细日志
        log_group = QGroupBox("处理日志")
        log_layout = QVBoxLayout()
        
        self.process_log = QTextEdit()
        self.process_log.setReadOnly(True)
        log_layout.addWidget(self.process_log)
        
        log_group.setLayout(log_layout)
        layout.addWidget(log_group)
        
        tab.setLayout(layout)
        return tab
        
    def setup_connections(self):
        """设置信号连接"""
        # 文件选择
        self.browse_button.clicked.connect(self.browse_video_file)
        self.output_browse_button.clicked.connect(self.browse_output_dir)
        
        # 字幕提取
        self.extract_button.clicked.connect(self.extract_subtitles)
        self.subtitle_list.currentTextChanged.connect(self.load_selected_subtitle)
        
        # WER计算
        self.load_subtitle_button.clicked.connect(self.load_subtitle_text)
        self.load_recognition_button.clicked.connect(self.load_recognition_text)
        self.calculate_button.clicked.connect(self.calculate_wer)
        
    def browse_video_file(self):
        """浏览视频文件"""
        file_path, _ = QFileDialog.getOpenFileName(
            self, 
            "选择视频文件",
            "",
            "视频文件 (*.mp4 *.avi *.mkv *.mov *.wmv *.flv *.webm);;所有文件 (*)"
        )
        
        if file_path:
            self.current_video_path = file_path
            self.file_path_edit.setText(file_path)
            self.media_player.load_media(file_path)
            self.statusBar().showMessage(f"已加载视频: {Path(file_path).name}")
            
    def browse_output_dir(self):
        """浏览输出目录"""
        dir_path = QFileDialog.getExistingDirectory(self, "选择输出目录")
        if dir_path:
            self.output_dir_edit.setText(dir_path)
            
    def extract_subtitles(self):
        """提取字幕"""
        if not self.current_video_path:
            QMessageBox.warning(self, "警告", "请先选择视频文件")
            return
            
        output_dir = self.output_dir_edit.text().strip() or None
        
        # 获取OCR设置
        ocr_settings = self._get_ocr_settings()
        
        # 禁用按钮
        self.extract_button.setEnabled(False)
        self.extract_button.setText("正在提取...")
        
        # 清空日志
        self.process_log.clear()
        
        # 启动提取线程
        self.extraction_thread = SubtitleExtractionThread(
            self.current_video_path, 
            output_dir,
            ocr_settings
        )
        self.extraction_thread.progress.connect(self.process_log.append)
        self.extraction_thread.finished.connect(self.on_extraction_finished)
        self.extraction_thread.error.connect(self.on_extraction_error)
        self.extraction_thread.start()
    
    def _get_ocr_settings(self):
        """获取PaddleOCR设置"""
        # 解析预设配置选择
        preset_map = {
            "subtitle (字幕专用)": 'subtitle',
            "fast (快速模式)": 'fast',
            "accurate (精确模式)": 'accurate',
            "balanced (平衡模式)": 'balanced'
        }
        
        selected_preset = self.ocr_preset_combo.currentText()
        preset = preset_map.get(selected_preset, 'subtitle')
        
        # 构建自定义参数（覆盖预设配置）
        custom_params = {}
        
        # 如果用户修改了默认值，则添加到自定义参数中
        if self.ocr_confidence_spin.value() != 0.2:
            custom_params['text_rec_score_thresh'] = self.ocr_confidence_spin.value()
        
        return {
            'preset': preset,
            'custom_params': custom_params,
            'sample_interval': self.ocr_interval_spin.value(),
            'confidence_threshold': self.ocr_confidence_spin.value(),
            'use_gpu': self.ocr_gpu_checkbox.isChecked(),
            'subtitle_region': None  # 可以在未来版本中添加手动选择区域功能
        }
        
    def on_extraction_finished(self, subtitle_files: List[str]):
        """字幕提取完成"""
        self.extract_button.setEnabled(True)
        self.extract_button.setText("提取字幕")
        
        self.current_subtitle_files = subtitle_files
        
        # 更新字幕文件列表
        self.subtitle_list.clear()
        if subtitle_files:
            for i, file_path in enumerate(subtitle_files):
                self.subtitle_list.addItem(f"字幕流 {i}: {Path(file_path).name}")
        else:
            self.subtitle_list.addItem("未提取到字幕")
            
        self.statusBar().showMessage(f"字幕提取完成，共 {len(subtitle_files)} 个文件")
        
    def on_extraction_error(self, error_msg: str):
        """字幕提取错误"""
        self.extract_button.setEnabled(True)
        self.extract_button.setText("提取字幕")
        
        QMessageBox.critical(self, "错误", error_msg)
        self.statusBar().showMessage("字幕提取失败")
        
    def load_selected_subtitle(self, selected_text: str):
        """加载选中的字幕"""
        if not selected_text or selected_text == "未提取到字幕":
            return
            
        # 从选择的文本中解析索引
        try:
            index = int(selected_text.split(":")[0].split()[-1])
            if 0 <= index < len(self.current_subtitle_files):
                subtitle_file = self.current_subtitle_files[index]
                
                # 解析字幕文件
                parser = SubtitleParser()
                texts = parser.parse_subtitle_file(subtitle_file)
                subtitle_text = ' '.join(texts)
                
                # 设置到文本框
                self.subtitle_text_edit.setText(subtitle_text)
                
                self.statusBar().showMessage(f"已加载字幕: {Path(subtitle_file).name}")
                
        except (ValueError, IndexError) as e:
            self.process_log.append(f"加载字幕时发生错误: {e}")
            
    def load_subtitle_text(self):
        """从文件加载字幕文本"""
        file_path, _ = QFileDialog.getOpenFileName(
            self,
            "选择字幕文件",
            "",
            "字幕文件 (*.srt *.ass *.ssa *.vtt);;所有文件 (*)"
        )
        
        if file_path:
            try:
                parser = SubtitleParser()
                texts = parser.parse_subtitle_file(file_path)
                subtitle_text = ' '.join(texts)
                self.subtitle_text_edit.setText(subtitle_text)
                self.statusBar().showMessage(f"已加载字幕文件: {Path(file_path).name}")
            except Exception as e:
                QMessageBox.critical(self, "错误", f"加载字幕文件失败: {e}")
                
    def load_recognition_text(self):
        """从文件加载识别结果文本"""
        file_path, _ = QFileDialog.getOpenFileName(
            self,
            "选择识别结果文件",
            "",
            "文本文件 (*.txt);;所有文件 (*)"
        )
        
        if file_path:
            try:
                with open(file_path, 'r', encoding='utf-8') as f:
                    text = f.read().strip()
                    self.recognition_text_edit.setText(text)
                    self.statusBar().showMessage(f"已加载识别结果: {Path(file_path).name}")
            except Exception as e:
                QMessageBox.critical(self, "错误", f"加载识别结果文件失败: {e}")
                
    def calculate_wer(self):
        """计算WER"""
        subtitle_text = self.subtitle_text_edit.toPlainText().strip()
        recognition_text = self.recognition_text_edit.toPlainText().strip()
        
        if not subtitle_text or not recognition_text:
            QMessageBox.warning(self, "警告", "请输入参考文本和识别结果文本")
            return
            
        # 禁用按钮
        self.calculate_button.setEnabled(False)
        self.calculate_button.setText("正在计算...")
        
        # 清空日志
        self.process_log.clear()
        
        # 启动计算线程
        self.wer_thread = WERCalculationThread(subtitle_text, recognition_text)
        self.wer_thread.progress.connect(self.process_log.append)
        self.wer_thread.finished.connect(self.on_wer_finished)
        self.wer_thread.error.connect(self.on_wer_error)
        self.wer_thread.start()
        
    def on_wer_finished(self, wer: float, stats: dict):
        """WER计算完成"""
        self.calculate_button.setEnabled(True)
        self.calculate_button.setText("计算WER")
        
        # 更新结果显示
        self.wer_value_label.setText(f"{wer:.4f} ({wer*100:.2f}%)")
        self.total_words_label.setText(str(stats['total_words']))
        self.correct_words_label.setText(str(stats['correct_words']))
        self.substitutions_label.setText(str(stats['substitutions']))
        self.deletions_label.setText(str(stats['deletions']))
        self.insertions_label.setText(str(stats['insertions']))
        
        # 根据WER值设置颜色
        if wer <= 0.05:  # 5%以下，绿色
            color = "green"
        elif wer <= 0.15:  # 15%以下，橙色
            color = "orange"
        else:  # 15%以上，红色
            color = "red"
            
        self.wer_value_label.setStyleSheet(f"font-size: 16px; font-weight: bold; color: {color};")
        
        self.statusBar().showMessage(f"WER计算完成: {wer:.4f} ({wer*100:.2f}%)")
        
    def on_wer_error(self, error_msg: str):
        """WER计算错误"""
        self.calculate_button.setEnabled(True)
        self.calculate_button.setText("计算WER")
        
        QMessageBox.critical(self, "错误", error_msg)
        self.statusBar().showMessage("WER计算失败")


def main():
    """主函数"""
    app = QApplication(sys.argv)
    
    # 设置应用程序属性
    app.setApplicationName("字幕WER计算工具")
    app.setApplicationVersion("2.0")
    
    # 创建主窗口
    window = SubtitleWERMainWindow()
    window.show()
    
    # 运行应用程序
    sys.exit(app.exec_())


if __name__ == '__main__':
    main() 