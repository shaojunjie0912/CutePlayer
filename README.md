# CutePlayer - 基于FFmpeg和SDL2的简易视频播放器

## 项目简介

CutePlayer是一个使用C++编写的简易视频播放器，基于FFmpeg和SDL2库开发。它能够播放常见的视频格式，并实现了基本的音视频同步功能。该项目主要用于学习音视频开发和多媒体应用的原理。

## 特性

- 基于FFmpeg的音视频解码
- 基于SDL2的音视频播放
- 多线程处理（读取线程、视频解码线程）
- 音视频同步
- 自适应窗口大小

## 依赖项

- FFmpeg (libavformat, libavcodec, libavutil, libswresample)
- SDL2
- fmt
- C++20兼容的编译器
- xmake (构建系统)

## 构建与运行

### 安装依赖项

```bash
# Ubuntu/Debian
sudo apt-get install libavformat-dev libavcodec-dev libavutil-dev libswresample-dev libsdl2-dev libfmt-dev

# Arch Linux
sudo pacman -S ffmpeg sdl2 fmt
```

### 安装xmake

```bash
# 使用官方安装脚本
bash <(curl -fsSL https://xmake.io/shget.text)

# 或者通过包管理器
# Ubuntu/Debian
sudo apt install xmake
# Arch Linux
sudo pacman -S xmake
```

### 构建项目

```bash
# 克隆仓库
git clone https://github.com/yourusername/CutePlayer.git
cd CutePlayer

# 构建
xmake

# 运行
xmake run player /path/to/your/video.mp4
```

## 项目结构

```
CutePlayer/
├── include/player/       # 头文件目录
│   ├── audio_thread.hpp  # 音频处理相关
│   ├── common.hpp        # 公共函数和定义
│   ├── const.hpp         # 常量定义
│   ├── core.hpp          # 核心数据结构和类
│   ├── ffmpeg.hpp        # FFmpeg库封装
│   ├── mtx_queue.hpp     # 线程安全队列
│   ├── read_thread.hpp   # 读取线程
│   └── video_thread.hpp  # 视频处理相关
├── src/                  # 源代码目录
│   ├── audio_thread.cpp  # 音频线程实现
│   ├── common.cpp        # 公共函数实现
│   ├── core.cpp          # 核心功能实现
│   ├── main.cpp          # 程序入口
│   ├── read_thread.cpp   # 读取线程实现
│   └── video_thread.cpp  # 视频线程实现
├── data/                 # 存放测试视频的目录
├── .clang-format         # 代码格式配置
├── .gitignore           # Git忽略文件
├── .vscode/             # VSCode配置
├── .xmake/              # xmake缓存目录
└── xmake.lua            # xmake构建配置
```

## 实现原理

CutePlayer的实现基于FFmpeg的解码能力和SDL2的显示和音频播放功能，主要包含以下几个核心组件：

1. **读取线程 (ReadThread)**
   - 从媒体文件读取原始数据包
   - 将音频和视频数据包分别放入对应的数据包队列

2. **视频解码线程 (DecodeThread)**
   - 从视频数据包队列中获取数据
   - 解码视频帧
   - 处理视频同步
   - 将解码后的视频帧放入视频帧队列

3. **音频回调函数**
   - SDL音频设备通过回调获取音频数据
   - 解码音频帧并进行重采样
   - 计算音频时钟用于同步

4. **视频显示**
   - 通过定时器控制视频帧显示节奏
   - 基于音频时钟计算视频延迟
   - 使用SDL渲染视频帧

5. **音视频同步**
   - 使用音频时钟作为主时钟
   - 根据音频时钟调整视频帧显示时机
   - 实现视频与音频的同步播放

## 使用示例

```bash
# 播放本地视频文件
xmake run player /path/to/video.mp4
```

## 待改进功能

- 播放控制（暂停、快进、快退）
- 进度条和时间显示
- 音量控制
- 全屏切换
- 更多格式支持和错误处理

## 许可证

[根据您的选择添加适当的许可证信息]
