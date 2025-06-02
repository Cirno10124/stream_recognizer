import os
import sys
import json
import time
import wave
import threading
import tkinter as tk
from tkinter import filedialog, scrolledtext, ttk
from tkinter import messagebox
import datetime
import subprocess
from pathlib import Path
from openai import OpenAI

class MediaTranscriber:
    def __init__(self, api_key):
        self.client = OpenAI(api_key=api_key)
        self.temp_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "temp")
        os.makedirs(self.temp_dir, exist_ok=True)
        
    def extract_audio(self, video_path):
        """从视频中提取音频"""
        filename = os.path.basename(video_path)
        base_name = os.path.splitext(filename)[0]
        audio_path = os.path.join(self.temp_dir, f"{base_name}_audio.wav")
        
        try:
            # 检查ffmpeg是否可用
            try:
                subprocess.run(["ffmpeg", "-version"], stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
            except (subprocess.SubprocessError, FileNotFoundError):
                raise Exception("ffmpeg未安装或不可用，请安装ffmpeg后再试")
            
            # 使用ffmpeg提取音频
            command = [
                "ffmpeg", "-i", video_path, 
                "-vn",               # 不处理视频
                "-acodec", "pcm_s16le",  # 使用16位PCM编码
                "-ar", "16000",      # 16kHz采样率
                "-ac", "1",          # 单声道
                "-y",                # 覆盖已有文件
                audio_path
            ]
            
            process = subprocess.run(
                command, 
                stdout=subprocess.PIPE, 
                stderr=subprocess.PIPE,
                text=True
            )
            
            if process.returncode != 0:
                raise Exception(f"ffmpeg错误: {process.stderr}")
                
            if not os.path.exists(audio_path) or os.path.getsize(audio_path) == 0:
                raise Exception("音频提取失败，提取的文件为空")
                
            return audio_path
        except Exception as e:
            raise Exception(f"音频提取失败: {str(e)}")
    
    def transcribe_audio(self, audio_path):
        """识别音频文件"""
        if not os.path.exists(audio_path):
            raise Exception(f"文件不存在: {audio_path}")
        
        file_size = os.path.getsize(audio_path)
        if file_size == 0:
            raise Exception("音频文件为空")
        
        try:
            with open(audio_path, "rb") as audio_file:
                transcript = self.client.audio.transcriptions.create(
                    model="whisper-1",
                    file=audio_file,
                    response_format="text"
                )
            
            return transcript
        except Exception as e:
            raise Exception(f"识别失败: {str(e)}")
    
    def cleanup(self):
        """清理临时文件"""
        for file in os.listdir(self.temp_dir):
            file_path = os.path.join(self.temp_dir, file)
            if os.path.isfile(file_path):
                try:
                    os.remove(file_path)
                except Exception:
                    pass

class TranscriberGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("媒体文件语音识别工具")
        self.root.geometry("800x600")
        
        self.transcriber = None
        self.setup_ui()
        
    def setup_ui(self):
        # 主框架
        main_frame = ttk.Frame(self.root, padding="10")
        main_frame.pack(fill=tk.BOTH, expand=True)
        
        # API密钥输入
        api_frame = ttk.Frame(main_frame)
        api_frame.pack(fill=tk.X, pady=5)
        
        ttk.Label(api_frame, text="OpenAI API密钥:").pack(side=tk.LEFT, padx=5)
        self.api_key_var = tk.StringVar(value=os.environ.get("OPENAI_API_KEY", ""))
        self.api_key_entry = ttk.Entry(api_frame, textvariable=self.api_key_var, width=50, show="*")
        self.api_key_entry.pack(side=tk.LEFT, padx=5, fill=tk.X, expand=True)
        
        # 显示/隐藏密钥按钮
        self.show_key = tk.BooleanVar(value=False)
        ttk.Checkbutton(api_frame, text="显示", variable=self.show_key, 
                        command=self.toggle_key_visibility).pack(side=tk.LEFT)
        
        # 文件选择
        file_frame = ttk.Frame(main_frame)
        file_frame.pack(fill=tk.X, pady=10)
        
        ttk.Label(file_frame, text="媒体文件:").pack(side=tk.LEFT, padx=5)
        self.file_path_var = tk.StringVar()
        self.file_entry = ttk.Entry(file_frame, textvariable=self.file_path_var, width=50)
        self.file_entry.pack(side=tk.LEFT, padx=5, fill=tk.X, expand=True)
        
        ttk.Button(file_frame, text="浏览", command=self.browse_file).pack(side=tk.LEFT, padx=5)
        
        # 语言选择
        lang_frame = ttk.Frame(main_frame)
        lang_frame.pack(fill=tk.X, pady=5)
        
        ttk.Label(lang_frame, text="语言:").pack(side=tk.LEFT, padx=5)
        self.language_var = tk.StringVar(value="zh")
        languages = [("中文", "zh"), ("英文", "en"), ("日语", "ja"), ("韩语", "ko")]
        
        for text, value in languages:
            ttk.Radiobutton(lang_frame, text=text, variable=self.language_var, 
                           value=value).pack(side=tk.LEFT, padx=10)
        
        # 识别按钮
        btn_frame = ttk.Frame(main_frame)
        btn_frame.pack(fill=tk.X, pady=10)
        
        self.transcribe_btn = ttk.Button(btn_frame, text="开始识别", command=self.start_transcription)
        self.transcribe_btn.pack(side=tk.LEFT, padx=5)
        
        self.save_btn = ttk.Button(btn_frame, text="保存结果", command=self.save_result, state=tk.DISABLED)
        self.save_btn.pack(side=tk.LEFT, padx=5)
        
        self.clear_btn = ttk.Button(btn_frame, text="清空", command=self.clear_result)
        self.clear_btn.pack(side=tk.LEFT, padx=5)
        
        # 进度条
        self.progress_var = tk.DoubleVar(value=0.0)
        self.progress = ttk.Progressbar(main_frame, orient=tk.HORIZONTAL, 
                                       length=100, mode='indeterminate',
                                       variable=self.progress_var)
        self.progress.pack(fill=tk.X, pady=5)
        
        # 识别结果文本框
        result_frame = ttk.LabelFrame(main_frame, text="识别结果")
        result_frame.pack(fill=tk.BOTH, expand=True, pady=10)
        
        self.result_text = scrolledtext.ScrolledText(result_frame, wrap=tk.WORD, height=10)
        self.result_text.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        
        # 状态栏
        self.status_var = tk.StringVar(value="就绪")
        status_bar = ttk.Label(self.root, textvariable=self.status_var, relief=tk.SUNKEN, anchor=tk.W)
        status_bar.pack(side=tk.BOTTOM, fill=tk.X)
        
    def toggle_key_visibility(self):
        if self.show_key.get():
            self.api_key_entry.config(show="")
        else:
            self.api_key_entry.config(show="*")
    
    def browse_file(self):
        file_types = [
            ("媒体文件", "*.mp4 *.avi *.mkv *.mov *.mp3 *.wav *.ogg *.m4a"),
            ("视频文件", "*.mp4 *.avi *.mkv *.mov"),
            ("音频文件", "*.mp3 *.wav *.ogg *.m4a"),
            ("所有文件", "*.*")
        ]
        file_path = filedialog.askopenfilename(filetypes=file_types)
        if file_path:
            self.file_path_var.set(file_path)
    
    def start_transcription(self):
        api_key = self.api_key_var.get().strip()
        if not api_key:
            messagebox.showerror("错误", "请输入OpenAI API密钥")
            return
        
        file_path = self.file_path_var.get().strip()
        if not file_path or not os.path.exists(file_path):
            messagebox.showerror("错误", "请选择有效的媒体文件")
            return
        
        # 禁用按钮，显示进度条
        self.transcribe_btn.config(state=tk.DISABLED)
        self.progress.start(10)
        self.status_var.set("处理中...")
        
        # 创建转录器实例
        if not self.transcriber:
            self.transcriber = MediaTranscriber(api_key=api_key)
        
        # 在新线程中处理以避免UI冻结
        threading.Thread(target=self.process_file, args=(file_path,), daemon=True).start()
    
    def process_file(self, file_path):
        try:
            # 更新状态
            self.update_status("正在分析媒体文件...")
            
            # 判断是视频还是音频
            audio_path = file_path
            is_video = False
            ext = os.path.splitext(file_path)[1].lower()
            if ext in ['.mp4', '.avi', '.mkv', '.mov', '.flv', '.webm']:
                is_video = True
                self.update_status("正在从视频提取音频...")
                audio_path = self.transcriber.extract_audio(file_path)
            
            # 识别音频
            self.update_status("正在识别音频...")
            transcript = self.transcriber.transcribe_audio(audio_path)
            
            # 更新结果
            self.update_result(transcript)
            self.update_status("识别完成")
            
            # 如果是视频提取的音频，清理临时文件
            if is_video and os.path.exists(audio_path):
                try:
                    os.remove(audio_path)
                except:
                    pass
                
        except Exception as e:
            self.update_status(f"错误: {str(e)}")
            messagebox.showerror("处理失败", str(e))
        finally:
            # 恢复UI状态
            self.root.after(0, self.reset_ui)
    
    def update_status(self, message):
        self.root.after(0, lambda: self.status_var.set(message))
    
    def update_result(self, text):
        def _update():
            self.result_text.delete(1.0, tk.END)
            self.result_text.insert(tk.END, text)
            self.save_btn.config(state=tk.NORMAL)
        self.root.after(0, _update)
    
    def reset_ui(self):
        self.progress.stop()
        self.transcribe_btn.config(state=tk.NORMAL)
    
    def save_result(self):
        text = self.result_text.get(1.0, tk.END).strip()
        if not text:
            messagebox.showinfo("提示", "没有可保存的内容")
            return
        
        file_path = filedialog.asksaveasfilename(
            defaultextension=".txt",
            filetypes=[("文本文件", "*.txt"), ("所有文件", "*.*")]
        )
        
        if file_path:
            try:
                with open(file_path, 'w', encoding='utf-8') as f:
                    f.write(text)
                messagebox.showinfo("成功", f"结果已保存至: {file_path}")
            except Exception as e:
                messagebox.showerror("保存失败", str(e))
    
    def clear_result(self):
        self.result_text.delete(1.0, tk.END)
        self.save_btn.config(state=tk.DISABLED)

def main():
    root = tk.Tk()
    app = TranscriberGUI(root)
    root.mainloop()

if __name__ == "__main__":
    main()