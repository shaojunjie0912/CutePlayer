# CutePlayer 技术设计文档

## 目录

- [概述](#概述)
- [系统架构](#系统架构)
- [核心模块设计](#核心模块设计)
- [数据结构详解](#数据结构详解)
- [线程模型](#线程模型)
- [音视频同步机制](#音视频同步机制)
- [内存管理策略](#内存管理策略)
- [性能优化](#性能优化)
- [错误处理](#错误处理)

## 概述

CutePlayer 是一个基于 FFmpeg 和 SDL2 的轻量级媒体播放器，采用现代 C++20 标准开发。项目核心目标是实现高性能的音视频同步播放，同时保持代码的可读性和可维护性。

### 设计原则

1. **解耦设计**: 读取、解码、渲染分离
2. **线程安全**: 多线程环境下的数据安全
3. **资源管理**: RAII 原则和智能指针使用
4. **性能优先**: 减少内存拷贝，优化缓冲策略

## 系统架构

### 整体架构图

```
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│   文件读取线程   │───▶│   数据包队列     │───▶│   解码线程       │
│   ReadThread    │    │   PacketQueue   │    │   DecodeThread  │
└─────────────────┘    └─────────────────┘    └─────────────────┘
                                                        │
                                                        ▼
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│   SDL事件循环   │◀───│   视频帧队列     │◀───│   视频帧缓冲     │
│   EventLoop     │    │   FrameQueue    │    │   Frame Buffer  │
└─────────────────┘    └─────────────────┘    └─────────────────┘
        │
        ▼
┌─────────────────┐    ┌─────────────────┐
│   音频回调线程   │◀───│   音频缓冲区     │
│   AudioCallback │    │   Audio Buffer  │
└─────────────────┘    └─────────────────┘
```

### 模块依赖关系

```cpp
// 核心依赖层次
main.cpp
  ├── read_thread.hpp
  │   ├── video_thread.hpp
  │   ├── audio_thread.hpp
  │   └── core.hpp
  │       ├── ffmpeg.hpp
  │       └── const.hpp
  └── common.hpp
```

## 核心模块设计

### 1. 主程序模块 (main.cpp)

**职责**:

- SDL 子系统初始化
- 窗口和渲染器创建
- 程序生命周期管理

**关键流程**:

```cpp
int main(int argc, char* argv[]) {
    // 1. 参数验证
    // 2. SDL 初始化 (VIDEO | AUDIO | TIMER)
    // 3. 创建窗口和渲染器
    // 4. 启动媒体流处理
    // 5. 进入事件循环
}
```

### 2. 文件读取模块 (read_thread.cpp)

**职责**:

- 媒体文件解析和流信息提取
- 数据包读取和分发
- 解码器初始化和管理

**核心函数**:

```cpp
// 打开媒体流
VideoState* OpenStream(const std::string& file_name);

// 读取线程主循环
int ReadThread(void* arg);

// 打开流组件（音频/视频）
int OpenStreamComponent(VideoState* video_state, uint32_t stream_index);
```

**处理流程**:

1. 使用 `avformat_open_input()` 打开文件
2. 使用 `avformat_find_stream_info()` 获取流信息
3. 遍历流，找到音频和视频流索引
4. 为每个流创建对应的解码器
5. 循环读取数据包并分发到相应队列

### 3. 视频处理模块 (video_thread.cpp)

**职责**:

- 视频数据包解码
- 视频帧队列管理
- 音视频同步控制
- SDL 纹理渲染

**核心函数**:

```cpp
// 视频解码线程
int DecodeThread(void* arg);

// 视频刷新定时器
void VideoRefreshTimer(void* user_data);

// 视频显示
void DisplayVideo(VideoState* video_state);

// 视频同步
double SyschronizeVideo(VideoState* video_state, AVFrame* frame, double pts);
```

**同步算法**:

```cpp
// 计算时间差
double diff = video_pts - audio_clock;

// 同步策略
if (fabs(diff) < AV_NOSYNC_THRESHOLD) {
    if (diff <= -sync_threshold) {
        delay = 0;  // 视频落后，立即显示
    } else if (diff >= sync_threshold) {
        delay = 2 * delay;  // 视频超前，增加延迟
    }
}
```

### 4. 音频处理模块 (audio_thread.cpp)

**职责**:

- 音频数据包解码
- 音频重采样
- 音频时钟计算
- SDL 音频回调处理

**核心函数**:

```cpp
// 音频解码
int AudioDecodeFrame(VideoState* video_state);

// SDL 音频回调
void MyAudioCallback(void* userdata, uint8_t* stream, int len);

// 打开音频设备
int OpenAudio(void* opaque, AVChannelLayout* wanted_channel_layout, int wanted_sample_rate);
```

**音频处理流程**:

1. 从音频包队列获取数据包
2. 使用 `avcodec_send_packet()` 和 `avcodec_receive_frame()` 解码
3. 使用 SwrContext 进行重采样（转换为 S16 格式）
4. 计算音频时钟
5. 填充 SDL 音频缓冲区

## 数据结构详解

### VideoState - 全局状态管理

```cpp
struct VideoState {
    // 文件信息
    std::string file_name_;
    AVFormatContext *format_context_;
    
    // 流和解码器
    int video_stream_idx_{-1}, audio_stream_idx_{-1};
    AVStream *audio_stream_, *video_stream_;
    AVCodecContext *audio_codec_context_, *video_codec_context_;
    
    // 数据队列
    PacketQueue audio_packet_queue_, video_packet_queue_;
    FrameQueue video_frame_queue_;
    
    // 音频相关
    AVFrame audio_frame_;
    uint8_t *audio_buffer_;
    uint32_t audio_buffer_size_, audio_buffer_index_;
    SwrContext *audio_swr_context_;
    
    // 视频相关
    SDL_Texture *texture_;
    int x_left_, y_top_, width_, height_;
    
    // 同步时钟
    double audio_clock_, video_clock_;
    double frame_timer_, frame_last_delay_, frame_last_pts_;
    double video_current_pts_;
    int64_t video_current_pts_time_;
    
    // 线程管理
    SDL_Thread *read_tid_, *decode_tid_;
    bool quit_{false};
};
```

### PacketQueue - 线程安全的数据包队列

```cpp
struct PacketQueue {
    AVFifo *pkt_list_;       // FFmpeg FIFO 队列
    int nb_packets_;         // 包数量
    int size_;               // 总字节数
    int64_t duration_;       // 总时长
    std::mutex mtx_;         // 互斥锁
    std::condition_variable cv_;  // 条件变量
};
```

**关键操作**:

- `InitPacketQueue()`: 初始化队列，分配 FIFO 内存
- `PutPacketQueue()`: 线程安全地添加数据包
- `GetPacketQueue()`: 线程安全地获取数据包，支持阻塞/非阻塞模式
- `FlushPacketQueue()`: 清空队列，释放所有包

### FrameQueue - 视频帧环形缓冲区

```cpp
struct FrameQueue {
    Frame queue_[FRAME_QUEUE_SIZE];  // 帧数组
    int rindex_, windex_;            // 读写索引
    int size_, max_size_;            // 当前和最大大小
    int keep_last_;                  // 保留最后一帧标志
    int rindex_shown_;               // 已显示标志
    std::mutex mtx_;                 // 互斥锁
    std::condition_variable cv_notfull_, cv_notempty_;  // 条件变量
    PacketQueue *pktq_;              // 关联的包队列
};
```

**环形缓冲机制**:

```cpp
// 写入时的索引更新
void MoveWriteIndex(FrameQueue *f) {
    std::unique_lock lk{f->mtx_};
    if (++f->windex_ == f->max_size_) {
        f->windex_ = 0;  // 回绕到开始
    }
    ++f->size_;
    f->cv_notempty_.notify_one();
}
```

### MtxQueue - 通用线程安全队列模板

```cpp
template <typename T, typename Queue = std::queue<T>>
class MtxQueue {
private:
    Queue queue_;
    std::mutex mtx_;
    std::condition_variable cv_notfull_, cv_notempty_;
    std::size_t limit_;

public:
    void Push(T value);                          // 阻塞推入
    bool TryPush(T value);                       // 非阻塞推入
    T Pop();                                     // 阻塞弹出
    std::optional<T> TryPop();                   // 非阻塞弹出
    std::optional<T> TryPopFor(duration timeout); // 超时弹出
};
```

## 线程模型

### 线程关系图

```
主线程 (Main Thread)
├── SDL 事件循环
├── 视频刷新定时器
└── 窗口渲染

读取线程 (Read Thread)
├── 文件 I/O 操作
├── 数据包解复用
└── 队列管理

视频解码线程 (Video Decode Thread)
├── 视频包解码
├── 帧队列管理
└── PTS 计算

音频回调线程 (Audio Callback Thread, SDL 内部)
├── 音频包解码
├── 重采样处理
└── 音频时钟更新
```

### 线程同步机制

1. **互斥锁 (std::mutex)**
   - 保护共享数据结构
   - 避免竞态条件

2. **条件变量 (std::condition_variable)**
   - 实现生产者-消费者模式
   - 避免忙等待

3. **原子操作**
   - quit 标志使用原子操作
   - 减少锁的开销

### 线程通信

```cpp
// 队列满/空状态通知
cv_notfull_.wait(lk, [this] { return queue_.size() < limit_; });
cv_notempty_.wait(lk, [this] { return !queue_.empty(); });

// SDL 自定义事件
SDL_Event event;
event.type = FF_REFRESH_EVENT;
event.user.data1 = video_state;
SDL_PushEvent(&event);
```

## 音视频同步机制

### 时钟系统

CutePlayer 使用**音频时钟作为主时钟**的同步策略：

```cpp
// 音频时钟计算
if (!isnan(audio_frame.pts)) {
    audio_clock = audio_frame.pts + 
                  (double)audio_frame.nb_samples / audio_frame.sample_rate;
}

// 视频时钟计算
video_clock = pts;
frame_delay = av_q2d(video_stream->time_base);
video_clock += frame_delay;
```

### 同步算法详解

```cpp
void VideoRefreshTimer(void* user_data) {
    VideoState* vs = static_cast<VideoState*>(user_data);
    
    // 1. 获取当前视频帧
    Frame* vp = PeekFrameQueue(&vs->video_frame_queue_);
    
    // 2. 计算帧延迟
    double delay = vp->pts_ - vs->frame_last_pts_;
    
    // 3. 获取音频时钟（主时钟）
    double ref_clock = vs->audio_clock_;
    
    // 4. 计算音视频时间差
    double diff = vp->pts_ - ref_clock;
    
    // 5. 同步调整
    double sync_threshold = (delay > MAX_AV_SYNC_THRESHOLD) ? 
                           delay : MAX_AV_SYNC_THRESHOLD;
    
    if (fabs(diff) < AV_NOSYNC_THRESHOLD) {
        if (diff <= -sync_threshold) {
            delay = 0;           // 视频落后，立即显示
        } else if (diff >= sync_threshold) {
            delay = 2 * delay;   // 视频超前，加倍延迟
        }
    }
    
    // 6. 更新帧定时器
    vs->frame_timer_ += delay;
    
    // 7. 计算实际延迟
    double actual_delay = vs->frame_timer_ - (av_gettime() / 1000000.0);
    if (actual_delay < 0.010) {
        actual_delay = 0.010;  // 最小延迟 10ms
    }
    
    // 8. 调度下次刷新
    RefreshSchedule(vs, (int)(actual_delay * 1000 + 0.5));
    
    // 9. 显示当前帧
    DisplayVideo(vs);
}
```

### 同步阈值配置

```cpp
constexpr double MAX_AV_SYNC_THRESHOLD = 0.1;   // 最大同步阈值 100ms
constexpr double MIN_AV_SYNC_THRESHOLD = 0.04;  // 最小同步阈值 40ms
constexpr double AV_NOSYNC_THRESHOLD = 10.0;    // 无同步阈值 10s
```

## 内存管理策略

### 1. 资源生命周期管理

```cpp
// FFmpeg 资源管理
class VideoState {
public:
    ~VideoState() {
        // 清理 AVFormatContext
        if (format_context_) {
            avformat_close_input(&format_context_);
        }
        
        // 清理编解码器上下文
        if (video_codec_context_) {
            avcodec_free_context(&video_codec_context_);
        }
        
        // 清理队列
        DestoryPacketQueue(&video_packet_queue_);
        DestoryPacketQueue(&audio_packet_queue_);
    }
};
```

### 2. 内存池和对象重用

```cpp
// 帧队列预分配
for (int i = 0; i < max_size_; i++) {
    if (!(f->queue_[i].frame_ = av_frame_alloc())) {
        return AVERROR(ENOMEM);
    }
}

// 包的移动语义避免拷贝
av_packet_move_ref(pkt1, pkt);
```

### 3. 已知的内存问题

```cpp
// TODO: 需要修复的内存泄漏
uint8_t** out = &video_state->audio_buffer_;  // audio_buffer_ 内存泄漏问题
av_fast_malloc(&video_state->audio_buffer_, &video_state->audio_buffer_size_, out_size);
```

## 性能优化

### 1. 队列大小限制

```cpp
constexpr int MAX_QUEUE_SIZE = 15 * 1024 * 1024;  // 15MB 限制

// 流量控制
if (video_state->audio_packet_queue_.size_ > MAX_QUEUE_SIZE ||
    video_state->video_packet_queue_.size_ > MAX_QUEUE_SIZE) {
    SDL_Delay(10);  // 等待消费者处理
    continue;
}
```

### 2. 缓冲策略

```cpp
constexpr int FRAME_QUEUE_SIZE = 16;           // 帧队列大小
constexpr int VIDEO_PICTURE_QUEUE_SIZE = 3;    // 视频图片队列大小
constexpr int SDL_AUDIO_BUFFER_SIZE = 1024;    // SDL 音频缓冲大小
```

### 3. 减少内存拷贝

```cpp
// 使用移动语义
av_packet_move_ref(dst, src);
av_frame_move_ref(dst_frame, src_frame);

// 直接操作 SDL 纹理
SDL_UpdateYUVTexture(texture_, nullptr, 
                     frame->data[0], frame->linesize[0],
                     frame->data[1], frame->linesize[1], 
                     frame->data[2], frame->linesize[2]);
```

## 错误处理

### 1. FFmpeg 错误处理

```cpp
int ret = avformat_open_input(&format_context, file_name.c_str(), nullptr, nullptr);
if (ret < 0) {
    av_log(nullptr, AV_LOG_ERROR, "avformat_open_input failed\n");
    return -1;
}
```

### 2. SDL 错误处理

```cpp
if (SDL_Init(sdl_init_flags)) {
    av_log(nullptr, AV_LOG_ERROR, "Could not initialize SDL - %s\n", SDL_GetError());
    return -1;
}
```

### 3. 线程安全的错误传播

```cpp
// 使用 quit 标志优雅退出
if (video_state->quit_) {
    break;
}

// 通过返回值传播错误
if (ret < 0) {
    av_log(nullptr, AV_LOG_ERROR, "Decode failed\n");
    return ret;
}
```

### 4. 资源清理

```cpp
// RAII 模式确保资源释放
void FlushPacketQueue(PacketQueue *q) {
    std::unique_lock lk{q->mtx_};
    MyAVPacketList pkt1;
    while (av_fifo_read(q->pkt_list_, &pkt1, 1) >= 0) {
        av_packet_free(&pkt1.pkt);  // 确保释放每个包
    }
    q->nb_packets_ = 0;
    q->size_ = 0;
    q->duration_ = 0;
}
```

## 配置和调优

### 1. 编译时配置

```cpp
// const.hpp 中的关键参数
constexpr int DEFAULT_WIDTH = 960;              // 默认窗口宽度
constexpr int DEFAULT_HEIGHT = 540;             // 默认窗口高度
constexpr int FRAME_QUEUE_SIZE = 16;            // 帧队列大小
constexpr int MAX_QUEUE_SIZE = 15 * 1024 * 1024; // 队列最大内存
```

### 2. 运行时配置

```cpp
// 日志级别设置
av_log_set_level(AV_LOG_INFO);  // 可改为 AV_LOG_DEBUG 调试

// SDL 渲染器配置
SDL_CreateRenderer(window, -1, 
    SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
```

### 3. 性能监控

```cpp
// 帧率计算
video_state->video_current_pts_time_ = av_gettime();

// 队列状态监控
av_log(nullptr, AV_LOG_DEBUG, "Queue size: audio=%d, video=%d\n",
       audio_packet_queue_.nb_packets_, video_packet_queue_.nb_packets_);
```

## 扩展接口设计

### 1. 播放控制接口

```cpp
class PlaybackController {
public:
    void Play();
    void Pause();
    void Stop();
    void Seek(double timestamp);
    void SetVolume(float volume);
    double GetPosition() const;
    double GetDuration() const;
};
```

### 2. 事件回调接口

```cpp
class PlayerEventListener {
public:
    virtual void OnPlaybackStarted() = 0;
    virtual void OnPlaybackPaused() = 0;
    virtual void OnPlaybackFinished() = 0;
    virtual void OnError(int error_code, const std::string& message) = 0;
};
```

### 3. 滤镜插件接口

```cpp
class VideoFilter {
public:
    virtual int ProcessFrame(AVFrame* input, AVFrame* output) = 0;
    virtual const char* GetName() const = 0;
};
```

---

本文档详细描述了 CutePlayer 的技术实现细节，为后续的维护和扩展提供了全面的技术参考。
