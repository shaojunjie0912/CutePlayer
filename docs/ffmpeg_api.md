# AVPlayer 项目中使用的 FFmpeg API 参考文档

本文档总结了 AVPlayer 项目中使用到的所有 FFmpeg API，包括函数、结构体和宏定义，并提供了详细的注释说明。

## 目录

1. [数据结构](#数据结构)
2. [格式相关 API](#格式相关-api)
3. [编解码器相关 API](#编解码器相关-api)
4. [包和帧处理 API](#包和帧处理-api)
5. [音频处理 API](#音频处理-api)
6. [工具函数 API](#工具函数-api)
7. [队列相关 API](#队列相关-api)
8. [重采样相关 API](#重采样相关-api)
9. [常量和宏定义](#常量和宏定义)

---

## 数据结构

### AVState

```cpp
struct AVState {
    std::string file_name_;            // 输入文件名
    AVFormatContext *format_context_;  // 封装格式上下文
    // 音视频流相关
    int video_stream_idx_{-1};         // 视频流索引
    int audio_stream_idx_{-1};         // 音频流索引
    AVStream *audio_stream_;           // 音频流指针
    AVStream *video_stream_;           // 视频流指针
    AVCodecContext *audio_codec_context_; // 音频编解码器上下文
    AVCodecContext *video_codec_context_; // 视频编解码器上下文
    // 队列相关
    PacketQueue audio_packet_queue_;   // 音频包队列
    PacketQueue video_packet_queue_;   // 视频包队列
    AVPacket audio_packet_;            // 当前音频包
    AVPacket video_packet_;            // 当前视频包
    FrameQueue video_frame_queue_;     // 视频帧队列
    // 音频相关
    AVFrame audio_frame_;              // 当前音频帧
    uint8_t *audio_buffer_;            // 音频缓冲区
    uint32_t audio_buffer_size_;       // 音频缓冲区大小
    uint32_t audio_buffer_index_;      // 音频缓冲区索引
    struct SwrContext *audio_swr_context_; // 音频重采样上下文
    // 视频相关
    SDL_Texture *texture_;             // SDL 纹理对象
    // 同步相关
    double frame_timer_;               // 帧计时器
    double frame_last_delay_;          // 上一帧延迟
    double video_current_pts_;         // 当前视频 PTS
    int64_t video_current_pts_time_;   // 当前视频 PTS 对应的系统时间
    double frame_last_pts_;            // 上一帧 PTS
    double audio_clock_;               // 音频时钟
    double video_clock_;               // 视频时钟
    // 线程相关
    SDL_Thread *read_tid_;             // 读取线程
    SDL_Thread *decode_tid_;           // 解码线程
    bool quit_{false};                 // 退出标志
};
```

**说明**: 项目定义的主要状态结构，包含了播放器的所有状态信息。

### AVFormatContext

```cpp
AVFormatContext *format_context;
```

**说明**: FFmpeg 格式上下文结构，包含了输入文件的格式信息、流信息等。是解封装的核心数据结构。

### AVStream

```cpp
AVStream *stream;
```

**说明**: 表示媒体文件中的一个流（音频流或视频流）。包含流的编码参数、时间基等信息。

### AVCodec

```cpp
AVCodec const* codec;
```

**说明**: 编解码器结构，描述了特定的编解码器信息，包括编解码器 ID、能力等。

### AVCodecContext

```cpp
AVCodecContext *codec_context;
```

**说明**: 编解码器上下文，包含编解码器的配置参数，如分辨率、采样率、比特率等。

### AVCodecParameters

```cpp
AVCodecParameters *codec_params;
```

**说明**: 编解码器参数结构，包含流的编码参数，但不包含编解码器状态信息。

### AVPacket

```cpp
AVPacket packet;
```

**说明**: 压缩的数据包，包含编码后的音频或视频数据。从解封装器输出，送入解码器。

### AVFrame

```cpp
AVFrame *frame;
```

**说明**: 未压缩的音频或视频帧，是解码器的输出或编码器的输入。

### AVChannelLayout

```cpp
AVChannelLayout ch_layout;
```

**说明**: 音频通道布局结构，描述音频的通道配置（如立体声、5.1环绕声等）。

### AVRational

```cpp
AVRational time_base;
AVRational frame_rate;
AVRational aspect_ratio;
```

**说明**: 有理数结构，用于表示时间基、帧率、宽高比等比值。包含分子(num)和分母(den)。

### AVFifo

```cpp
AVFifo *pkt_list_;
```

**说明**: FFmpeg 提供的先进先出队列数据结构，支持自动增长，用于实现包队列。

### SwrContext

```cpp
struct SwrContext *audio_swr_context_;
```

**说明**: 音频重采样上下文，用于音频格式转换（采样率、通道数、采样格式等）。

---

## 格式相关 API

### avformat_open_input()

```cpp
int avformat_open_input(AVFormatContext **ps, const char *url, 
                        const AVInputFormat *fmt, AVDictionary **options);
```

**功能**: 打开输入文件并读取文件头，探测文件格式。
**参数**:

- `ps`: 指向 AVFormatContext 指针的指针，函数会分配并初始化
- `url`: 输入文件路径或 URL
- `fmt`: 指定输入格式，通常为 NULL 让 FFmpeg 自动探测
- `options`: 选项字典，通常为 NULL

**项目中使用**:

```cpp
ret = avformat_open_input(&format_context, video_state->file_name_.c_str(), nullptr, nullptr);
```

### avformat_find_stream_info()

```cpp
int avformat_find_stream_info(AVFormatContext *ic, AVDictionary **options);
```

**功能**: 读取媒体文件的包头，获取流信息。
**参数**:

- `ic`: 格式上下文
- `options`: 选项数组，通常为 NULL

**项目中使用**:

```cpp
ret = avformat_find_stream_info(format_context, nullptr);
```

### av_read_frame()

```cpp
int av_read_frame(AVFormatContext *s, AVPacket *pkt);
```

**功能**: 从输入文件读取一个数据包。
**参数**:

- `s`: 格式上下文
- `pkt`: 用于存储读取的数据包

**返回值**:

- `>= 0`: 成功
- `< 0`: 错误或文件结束

**项目中使用**:

```cpp
ret = av_read_frame(format_context, packet);
```

---

## 编解码器相关 API

### avcodec_find_decoder()

```cpp
const AVCodec *avcodec_find_decoder(enum AVCodecID id);
```

**功能**: 根据编解码器 ID 查找对应的解码器。
**参数**:

- `id`: 编解码器 ID

**返回值**: 解码器指针，失败返回 NULL

**项目中使用**:

```cpp
AVCodec const* codec{avcodec_find_decoder(codec_params->codec_id)};
```

### avcodec_alloc_context3()

```cpp
AVCodecContext *avcodec_alloc_context3(const AVCodec *codec);
```

**功能**: 分配并初始化编解码器上下文。
**参数**:

- `codec`: 编解码器指针

**返回值**: 编解码器上下文指针，失败返回 NULL

**项目中使用**:

```cpp
AVCodecContext* codec_context{avcodec_alloc_context3(codec)};
```

### avcodec_parameters_to_context()

```cpp
int avcodec_parameters_to_context(AVCodecContext *codec, 
                                  const AVCodecParameters *par);
```

**功能**: 将编解码器参数复制到编解码器上下文。
**参数**:

- `codec`: 目标编解码器上下文
- `par`: 源编解码器参数

**项目中使用**:

```cpp
ret = avcodec_parameters_to_context(codec_context, codec_params);
```

### avcodec_open2()

```cpp
int avcodec_open2(AVCodecContext *avctx, const AVCodec *codec, 
                  AVDictionary **options);
```

**功能**: 初始化编解码器上下文，使其可以使用。
**参数**:

- `avctx`: 编解码器上下文
- `codec`: 编解码器
- `options`: 选项字典

**项目中使用**:

```cpp
ret = avcodec_open2(codec_context, codec, nullptr);
```

### avcodec_send_packet()

```cpp
int avcodec_send_packet(AVCodecContext *avctx, const AVPacket *avpkt);
```

**功能**: 向解码器发送压缩数据包进行解码。
**参数**:

- `avctx`: 解码器上下文
- `avpkt`: 输入数据包

**项目中使用**:

```cpp
ret = avcodec_send_packet(video_state->audio_codec_context_, &video_state->audio_packet_);
```

### avcodec_receive_frame()

```cpp
int avcodec_receive_frame(AVCodecContext *avctx, AVFrame *frame);
```

**功能**: 从解码器接收解码后的帧。
**参数**:

- `avctx`: 解码器上下文
- `frame`: 输出帧

**返回值**:

- `>= 0`: 成功
- `AVERROR(EAGAIN)`: 需要更多输入
- `AVERROR_EOF`: 解码器已刷新

**项目中使用**:

```cpp
ret = avcodec_receive_frame(video_state->audio_codec_context_, &video_state->audio_frame_);
```

---

## 包和帧处理 API

### av_packet_alloc()

```cpp
AVPacket *av_packet_alloc(void);
```

**功能**: 分配 AVPacket 结构并初始化字段为默认值。
**返回值**: 新分配的 AVPacket 指针，失败返回 NULL

**项目中使用**:

```cpp
AVPacket* packet{av_packet_alloc()};
```

### av_packet_free()

```cpp
void av_packet_free(AVPacket **pkt);
```

**功能**: 释放 AVPacket 及其数据。
**参数**:

- `pkt`: 指向 AVPacket 指针的指针

**项目中使用**:

```cpp
av_packet_free(&packet);
```

### av_packet_unref()

```cpp
void av_packet_unref(AVPacket *pkt);
```

**功能**: 减少 AVPacket 引用计数，如果引用计数为 0 则释放数据。
**参数**:

- `pkt`: AVPacket 指针

**项目中使用**:

```cpp
av_packet_unref(packet);
```

### av_packet_move_ref()

```cpp
void av_packet_move_ref(AVPacket *dst, AVPacket *src);
```

**功能**: 将源包的引用移动到目标包，源包被重置。
**参数**:

- `dst`: 目标包
- `src`: 源包

**项目中使用**:

```cpp
av_packet_move_ref(pkt1, pkt);
```

### av_frame_alloc()

```cpp
AVFrame *av_frame_alloc(void);
```

**功能**: 分配 AVFrame 结构并初始化字段为默认值。
**返回值**: 新分配的 AVFrame 指针，失败返回 NULL

**项目中使用**:

```cpp
if (!(f->queue_[i].frame_ = av_frame_alloc())) {
    return AVERROR(ENOMEM);
}
```

### av_frame_unref()

```cpp
void av_frame_unref(AVFrame *frame);
```

**功能**: 减少 AVFrame 引用计数，如果引用计数为 0 则释放数据。
**参数**:

- `frame`: AVFrame 指针

**项目中使用**:

```cpp
av_frame_unref(&video_state->audio_frame_);
```

### av_frame_move_ref()

```cpp
void av_frame_move_ref(AVFrame *dst, AVFrame *src);
```

**功能**: 将源帧的引用移动到目标帧，源帧被重置。
**参数**:

- `dst`: 目标帧
- `src`: 源帧

**项目中使用**:

```cpp
av_frame_move_ref(vp->frame_, src_frame);
```

---

## 音频处理 API

### av_channel_layout_copy()

```cpp
int av_channel_layout_copy(AVChannelLayout *dst, const AVChannelLayout *src);
```

**功能**: 复制音频通道布局。
**参数**:

- `dst`: 目标通道布局
- `src`: 源通道布局

**项目中使用**:

```cpp
ret = av_channel_layout_copy(&ch_layout, &codec_context->ch_layout);
```

### av_samples_get_buffer_size()

```cpp
int av_samples_get_buffer_size(int *linesize, int nb_channels, int nb_samples,
                              enum AVSampleFormat sample_fmt, int align);
```

**功能**: 计算音频样本缓冲区所需的大小。
**参数**:

- `linesize`: 输出每行字节数
- `nb_channels`: 音频通道数
- `nb_samples`: 每通道样本数
- `sample_fmt`: 采样格式
- `align`: 缓冲区对齐

**项目中使用**:

```cpp
int out_size = av_samples_get_buffer_size(
    nullptr, video_state->audio_frame_.ch_layout.nb_channels, out_count,
    AV_SAMPLE_FMT_S16, 0);
```

### av_fast_malloc()

```cpp
void av_fast_malloc(void *ptr, unsigned int *size, size_t min_size);
```

**功能**: 快速内存分配，如果当前缓冲区大小不足则重新分配。
**参数**:

- `ptr`: 指向缓冲区指针的指针
- `size`: 指向当前缓冲区大小的指针
- `min_size`: 所需的最小大小

**项目中使用**:

```cpp
av_fast_malloc(&video_state->audio_buffer_, &video_state->audio_buffer_size_,
               out_size);
```

### av_get_bytes_per_sample()

```cpp
int av_get_bytes_per_sample(enum AVSampleFormat sample_fmt);
```

**功能**: 获取指定采样格式的每个样本字节数。
**参数**:

- `sample_fmt`: 采样格式

**返回值**: 每个样本的字节数

**项目中使用**:

```cpp
data_size = nb_ch_samples * video_state->audio_frame_.ch_layout.nb_channels *
            av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);
```

---

## 工具函数 API

### av_gettime()

```cpp
int64_t av_gettime(void);
```

**功能**: 获取当前时间（微秒）。
**返回值**: 当前时间的微秒表示

**项目中使用**:

```cpp
video_state->frame_timer_ = (double)av_gettime() / 1000000.0;
```

### av_q2d()

```cpp
static inline double av_q2d(AVRational a);
```

**功能**: 将 AVRational 转换为 double。
**参数**:

- `a`: AVRational 值

**返回值**: 对应的 double 值

**项目中使用**:

```cpp
frame_delay = av_q2d(video_state->video_stream_->time_base);
```

### av_make_q()

```cpp
static inline AVRational av_make_q(int num, int den);
```

**功能**: 创建 AVRational 值。
**参数**:

- `num`: 分子
- `den`: 分母

**返回值**: AVRational 值

**项目中使用**:

```cpp
if (av_cmp_q(aspect_ratio, av_make_q(0, 1)) <= 0) {
    aspect_ratio = av_make_q(1, 1);
}
```

### av_cmp_q()

```cpp
static inline int av_cmp_q(AVRational a, AVRational b);
```

**功能**: 比较两个 AVRational 值。
**参数**:

- `a`, `b`: 要比较的 AVRational 值

**返回值**:

- `< 0`: a < b
- `0`: a == b  
- `> 0`: a > b

**项目中使用**:

```cpp
if (av_cmp_q(aspect_ratio, av_make_q(0, 1)) <= 0) {
```

### av_mul_q()

```cpp
AVRational av_mul_q(AVRational b, AVRational c);
```

**功能**: 计算两个 AVRational 的乘积。
**参数**:

- `b`, `c`: 要相乘的 AVRational 值

**返回值**: 乘积结果

**项目中使用**:

```cpp
aspect_ratio = av_mul_q(aspect_ratio, av_make_q(picture_width, picture_height));
```

### av_rescale()

```cpp
int64_t av_rescale(int64_t a, int64_t b, int64_t c);
```

**功能**: 计算 a * b / c，避免溢出。
**参数**:

- `a`, `b`, `c`: 计算参数

**返回值**: 计算结果

**项目中使用**:

```cpp
width = av_rescale(height, aspect_ratio.num, aspect_ratio.den) & ~1;
```

### av_guess_sample_aspect_ratio()

```cpp
AVRational av_guess_sample_aspect_ratio(AVFormatContext *format, 
                                       AVStream *stream, AVFrame *frame);
```

**功能**: 猜测样本宽高比。
**参数**:

- `format`: 格式上下文
- `stream`: 流
- `frame`: 帧（可为 NULL）

**返回值**: 猜测的宽高比

**项目中使用**:

```cpp
AVRational sar{av_guess_sample_aspect_ratio(format_context, stream, nullptr)};
```

---

## 队列相关 API

### av_fifo_alloc2()

```cpp
AVFifo *av_fifo_alloc2(size_t nb_elems, size_t elem_size, unsigned int flags);
```

**功能**: 分配并初始化 FIFO 缓冲区。
**参数**:

- `nb_elems`: 初始元素数量
- `elem_size`: 每个元素的大小
- `flags`: 标志位（如 AV_FIFO_FLAG_AUTO_GROW）

**返回值**: FIFO 指针，失败返回 NULL

**项目中使用**:

```cpp
q->pkt_list_ = av_fifo_alloc2(1, sizeof(MyAVPacketList), AV_FIFO_FLAG_AUTO_GROW);
```

### av_fifo_write()

```cpp
int av_fifo_write(AVFifo *f, const void *buf, size_t nb_elems);
```

**功能**: 向 FIFO 写入数据。
**参数**:

- `f`: FIFO 指针
- `buf`: 数据缓冲区
- `nb_elems`: 要写入的元素数量

**返回值**: 写入的元素数量，错误返回负值

**项目中使用**:

```cpp
ret = av_fifo_write(q->pkt_list_, &pkt1, 1);
```

### av_fifo_read()

```cpp
int av_fifo_read(AVFifo *f, void *buf, size_t nb_elems);
```

**功能**: 从 FIFO 读取数据。
**参数**:

- `f`: FIFO 指针
- `buf`: 目标缓冲区
- `nb_elems`: 要读取的元素数量

**返回值**: 读取的元素数量，错误返回负值

**项目中使用**:

```cpp
if (av_fifo_read(q->pkt_list_, &pkt1, 1) >= 0) {
```

### av_fifo_freep2()

```cpp
void av_fifo_freep2(AVFifo **f);
```

**功能**: 释放 FIFO 并将指针设为 NULL。
**参数**:

- `f`: 指向 FIFO 指针的指针

**项目中使用**:

```cpp
av_fifo_freep2(&q->pkt_list_);
```

---

## 重采样相关 API

### swr_alloc_set_opts2()

```cpp
int swr_alloc_set_opts2(struct SwrContext **ps,
                        const AVChannelLayout *out_ch_layout, enum AVSampleFormat out_sample_fmt, int out_sample_rate,
                        const AVChannelLayout *in_ch_layout, enum AVSampleFormat in_sample_fmt, int in_sample_rate,
                        int log_offset, void *log_ctx);
```

**功能**: 分配并设置重采样上下文的参数。
**参数**:

- `ps`: 指向重采样上下文指针的指针
- `out_ch_layout`: 输出通道布局
- `out_sample_fmt`: 输出采样格式
- `out_sample_rate`: 输出采样率
- `in_ch_layout`: 输入通道布局
- `in_sample_fmt`: 输入采样格式
- `in_sample_rate`: 输入采样率
- `log_offset`: 日志偏移
- `log_ctx`: 日志上下文

**项目中使用**:

```cpp
swr_alloc_set_opts2(
    &video_state->audio_swr_context_, &out_ch_layout, AV_SAMPLE_FMT_S16,
    video_state->audio_codec_context_->sample_rate, &in_ch_layout,
    video_state->audio_codec_context_->sample_fmt,
    video_state->audio_codec_context_->sample_rate, 0, nullptr);
```

### swr_init()

```cpp
int swr_init(struct SwrContext *s);
```

**功能**: 初始化重采样上下文。
**参数**:

- `s`: 重采样上下文

**返回值**: 成功返回 0，错误返回负值

**项目中使用**:

```cpp
swr_init(video_state->audio_swr_context_);
```

### swr_convert()

```cpp
int swr_convert(struct SwrContext *s, uint8_t **out, int out_count,
                const uint8_t **in, int in_count);
```

**功能**: 执行音频重采样转换。
**参数**:

- `s`: 重采样上下文
- `out`: 输出缓冲区数组
- `out_count`: 输出样本数
- `in`: 输入缓冲区数组
- `in_count`: 输入样本数

**返回值**: 转换后的样本数，错误返回负值

**项目中使用**:

```cpp
int nb_ch_samples = swr_convert(video_state->audio_swr_context_, out, out_count, in, in_count);
```

---

## 常量和宏定义

### 媒体类型常量

```cpp
AVMEDIA_TYPE_VIDEO    // 视频媒体类型
AVMEDIA_TYPE_AUDIO    // 音频媒体类型
```

**说明**: 用于区分流的媒体类型。

### 采样格式常量

```cpp
AV_SAMPLE_FMT_S16     // 16位有符号整数采样格式
```

**说明**: 音频采样格式，S16 表示 16 位有符号整数。

### 错误码

```cpp
AVERROR(EAGAIN)       // 需要更多输入数据
AVERROR_EOF           // 文件结束
AVERROR(ENOMEM)       // 内存不足
```

**说明**: FFmpeg 标准错误码。

### 特殊值

```cpp
AV_NOPTS_VALUE        // 无效的 PTS 值
```

**说明**: 表示时间戳无效的特殊值。

### 队列标志

```cpp
AV_FIFO_FLAG_AUTO_GROW  // FIFO 自动增长标志
```

**说明**: 允许 FIFO 缓冲区自动增长。

### 宏函数

```cpp
FFMAX(a, b)           // 返回两个值中的最大值
```

**说明**: FFmpeg 定义的取最大值宏。

---

## 总结

本文档涵盖了 AVPlayer 项目中使用的所有主要 FFmpeg API。这些 API 主要分为以下几个功能模块：

1. **格式处理**: 文件打开、流信息获取、包读取
2. **编解码**: 解码器查找、上下文管理、编解码操作
3. **数据管理**: 包和帧的分配、引用管理
4. **音频处理**: 通道布局、重采样、格式转换
5. **工具函数**: 时间处理、数学运算、类型转换
6. **队列操作**: FIFO 缓冲区管理

这些 API 共同构成了一个完整的音视频播放器框架，实现了从文件读取到音视频同步播放的完整流程。
