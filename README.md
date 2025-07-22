# CutePlayer

一个基于 C++20 开发的轻量级多媒体播放器，支持音视频同步播放。

## 🎯 项目简介

CutePlayer 使用 FFmpeg 进行音视频解码，SDL2 进行窗口渲染和音频输出，采用多线程架构实现高效的媒体文件播放。项目展示了现代C++在多媒体编程中的应用，是学习音视频处理和FFmpeg的优秀示例。

## ✨ 特性

- 🎬 支持多种视频格式（MP4、AVI、MKV、MOV等）
- 🎵 支持多种音频格式（MP3、AAC、FLAC、WAV等）
- 🔄 音视频同步播放
- 🧵 多线程架构，性能优异
- 🖥️ 跨平台支持（Windows、Linux、macOS）
- 💾 智能内存管理，避免内存溢出
- 🎛️ 自适应窗口大小

## 🛠️ 技术栈

- **编程语言**: C++20
- **构建工具**: XMake
- **多媒体库**: FFmpeg（解码）、SDL2（渲染和音频）
- **日志库**: fmt
- **并发**: C++ STL（std::thread、std::mutex、std::condition_variable）

## 📁 项目结构

```
CutePlayer/
├── include/cuteplayer/          # 头文件目录
│   ├── core.hpp                 # 核心数据结构定义
│   ├── ffmpeg.hpp               # FFmpeg库封装
│   ├── const.hpp                # 常量定义
│   ├── common.hpp               # 通用函数声明
│   ├── mtx_queue.hpp            # 线程安全队列模板
│   ├── video_thread.hpp         # 视频线程相关声明
│   ├── audio_thread.hpp         # 音频线程相关声明
│   └── read_thread.hpp          # 文件读取线程声明
├── src/                         # 源码目录
│   ├── main.cpp                 # 程序入口
│   ├── core.cpp                 # 核心功能实现
│   ├── video_thread.cpp         # 视频处理线程
│   ├── audio_thread.cpp         # 音频处理线程
│   ├── read_thread.cpp          # 文件读取线程
│   └── common.cpp               # 通用工具函数
├── xmake.lua                    # 构建配置文件
└── README.md                    # 项目说明
```

## 🏗️ 架构设计

### 多线程架构

CutePlayer 采用多线程设计，实现了高效的并发处理：

1. **主线程**: SDL初始化、窗口创建和事件循环
2. **读取线程**: 从文件读取数据包并分发到音视频队列
3. **视频解码线程**: 视频数据包解码和帧队列管理
4. **音频回调线程**: 音频数据解码和播放（SDL内部）
5. **视频渲染线程**: 视频帧显示和音视频同步（定时器驱动）

### 数据流

```
媒体文件 → 读取线程 → 音频包队列 → 音频解码 → 音频播放
                  ↓
                视频包队列 → 视频解码 → 视频帧队列 → 视频显示
                                                    ↓
                                              音视频同步
```

## 🔧 构建和安装

### 环境要求

- C++20 兼容编译器（GCC 10+、Clang 10+、MSVC 2019+）
- XMake 构建工具
- FFmpeg 开发库
- SDL2 开发库

### 构建步骤

1. **安装依赖**

   Ubuntu/Debian:
   ```bash
   sudo apt update
   sudo apt install build-essential cmake
   sudo apt install libavcodec-dev libavformat-dev libavutil-dev libswresample-dev
   sudo apt install libsdl2-dev
   ```

   macOS:
   ```bash
   brew install ffmpeg sdl2
   ```

2. **安装 XMake**
   ```bash
   curl -fsSL https://xmake.io/shget.text | bash
   ```

3. **构建项目**
   ```bash
   cd CutePlayer
   xmake build
   ```

## 🚀 使用方法

```bash
# 运行播放器
./build/cuteplayer <媒体文件路径>

# 示例
./build/cuteplayer /path/to/video.mp4
```

### 支持的格式

播放器支持FFmpeg支持的所有格式，常见格式包括：

**视频格式**: MP4、AVI、MKV、MOV、WMV、FLV、WebM  
**音频格式**: MP3、AAC、FLAC、WAV、OGG、M4A

## 📚 核心组件详解

### VideoState - 核心状态管理

```cpp
struct VideoState {
    // 文件和流信息
    std::string file_name_;
    AVFormatContext *format_context_;
    
    // 音视频流
    AVStream *audio_stream_, *video_stream_;
    AVCodecContext *audio_codec_context_, *video_codec_context_;
    
    // 数据队列
    PacketQueue audio_packet_queue_, video_packet_queue_;
    FrameQueue video_frame_queue_;
    
    // 同步时钟
    double audio_clock_, video_clock_;
    
    // SDL渲染
    SDL_Texture *texture_;
    
    // 线程管理
    SDL_Thread *read_tid_, *decode_tid_;
};
```

### PacketQueue - 线程安全的包队列

使用FFmpeg的AVFifo实现线程安全的数据包队列：
- 支持音频和视频包的独立队列
- 内置流量控制，防止内存溢出
- 条件变量实现生产者-消费者模式

### FrameQueue - 视频帧缓冲

环形缓冲区实现的帧队列：
- 支持多帧缓冲提高播放流畅性
- 读写索引分离，支持并发访问
- keep_last机制优化帧显示

## 🎵 音视频同步算法

CutePlayer 使用音频时钟作为主时钟进行同步：

```cpp
// 计算音视频时间差
double diff = video_pts - audio_clock;

if (diff <= -sync_threshold) {
    // 视频落后，立即显示
    delay = 0;
} else if (diff >= sync_threshold) {
    // 视频超前，增加延迟
    delay = 2 * delay;
}
```

这种方法确保了音视频的同步播放，避免了唇音不同步的问题。

## 🔧 配置选项

主要配置常量在 `const.hpp` 中定义：

```cpp
constexpr int kDefaultWidth = 960;          // 默认窗口宽度
constexpr int kDefaultHeight = 540;         // 默认窗口高度
constexpr int kVideoPictureQueueSize = 3;   // 视频帧队列大小
constexpr int kMaxQueueSize = 15 * 1024 * 1024; // 最大队列大小(15MB)
constexpr double kMaxAvSyncThreshold = 0.1; // 音视频同步阈值
```

## 🐛 已知问题

- 部分场景下可能存在内存泄漏
- 暂不支持用户交互控制（暂停、快进等）
- 主时钟固定为音频时钟

## 🚧 未来计划

- [ ] 添加播放控制功能（暂停、快进、快退）
- [ ] 实现音量控制
- [ ] 支持字幕显示
- [ ] 添加播放列表功能
- [ ] 改进用户界面
- [ ] 修复内存泄漏问题
- [ ] 支持更多视频滤镜效果

## 🤝 贡献

欢迎提交 Issue 和 Pull Request！

1. Fork 本仓库
2. 创建功能分支 (`git checkout -b feature/AmazingFeature`)
3. 提交更改 (`git commit -m 'Add some AmazingFeature'`)
4. 推送到分支 (`git push origin feature/AmazingFeature`)
5. 打开 Pull Request

## 📄 许可证

本项目采用 [MIT 许可证](LICENSE)。

## 🙏 致谢

- [FFmpeg](https://ffmpeg.org/) - 强大的多媒体处理库
- [SDL2](https://www.libsdl.org/) - 跨平台多媒体库
- [XMake](https://xmake.io/) - 现代化的构建工具

## 📞 联系方式

如有问题或建议，请提交 Issue 或通过以下方式联系：

- 项目主页: [GitHub Repository](https://github.com/yourusername/CutePlayer)
- 问题反馈: [Issues](https://github.com/yourusername/CutePlayer/issues)

---

⭐ 如果这个项目对你有帮助，请给个 Star！ 