#pragma once

#include <condition_variable>
#include <mutex>
#include <string>

// ================== spdlog ==================
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

// ================== FFmpeg ==================
extern "C" {
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/fifo.h>
#include <libavutil/log.h>
#include <libavutil/time.h>
#include <libswresample/swresample.h>
}

// NOTE: AVFifo: FFmpeg 封装的通用队列(支持自动增长), 它不关心数据类型, 只关心数据大小(字节)

// ================== core ==================

// 包装的 MyAVPacketList 目前存储 AVPacket 指针, 支持后续扩展
struct MyAVPacketList {
    AVPacket *pkt;  // 未解码的压缩数据包
};

// 压缩包队列
struct PacketQueue {
    AVFifo *pkt_list_;  // 先进先出队列, 存储 MyAVPacketList
    int nb_packets_;    // 队列中当前的packet数
    int size_;          // 队列所有节点占用的总内存大小
    int64_t duration_;  // 队列中所有节点的合计时长
    std::mutex mtx_;
    std::condition_variable cv_;  // 队列是否为空的条件变量
};

// 解码帧类
struct Frame {
    AVFrame *frame_;   // 解码后的帧
    double pts_;       // 帧的显示时间戳
    double duration_;  // 帧的估计持续时间
    int64_t pos_;      // 帧在输入文件中的字节位置
    int width_;
    int height_;
    int format_;
    AVRational sar_;
};

// 解码帧环形队列
constexpr int kFrameQueueSize = 16;  // 解码帧环形队列默认大小 16 (用户配置为 3)
struct FrameQueue {
    Frame queue_[kFrameQueueSize];  // 数组实现环形队列
    int rindex_;                    // 读索引
    int windex_;                    // 写索引
    int size_;                      // 队列当前帧数
    int max_size_;                  // 队列最大帧数 (用户配置)

    int keep_last_;     // 播放后是否在队列中保留上一帧不销毁
    int rindex_shown_;  // keep_last的实现，读的时候实际上读的是rindex + rindex_shown

    std::mutex mtx_;
    std::condition_variable cv_notfull_;   // 队列是否为满的条件变量
    std::condition_variable cv_notempty_;  // 队列是否为空的条件变量
    PacketQueue *pktq_;                    // 关联的 PacketQueue
};

struct AVState {
    std::string file_name_;            // 文件名
    AVFormatContext *format_context_;  // 封装格式上下文

    // ================== Audio & Video ==================
    int video_stream_idx_{-1};
    int audio_stream_idx_{-1};

    AVStream *audio_stream_;
    AVStream *video_stream_;

    AVCodecContext *audio_codec_context_;
    AVCodecContext *video_codec_context_;

    PacketQueue audio_packet_queue_;
    PacketQueue video_packet_queue_;

    AVPacket audio_packet_;
    AVPacket video_packet_;

    // ================== Audio ==================
    AVFrame audio_frame_;
    uint8_t *audio_buffer_;
    uint32_t audio_buffer_size_;
    uint32_t audio_buffer_index_;
    struct SwrContext *audio_swr_context_;

    // ================== Video ==================
    FrameQueue video_frame_queue_;  // 解码后的视频帧队列

    // ================== SDL ==================
    int x_left_;  // 播放器窗口左上角 x 坐标
    int y_top_;   // 播放器窗口左上角 y 坐标
    int width_;   // 播放器窗口宽度
    int height_;  // 播放器窗口高度

    SDL_Texture *texture_;

    // ================== Sync ==================
    // NOTE: 写死了主时钟为音频时钟
    double frame_timer_;              // 最后一帧播放的时刻(现在视频播放了多长时间)
    double frame_last_delay_;         // 最后一帧滤波延迟(上一次渲染视频帧delay时间)
    double video_current_pts_;        // 当前 pts
    int64_t video_current_pts_time_;  // 系统时间
    double frame_last_pts_;           // 上一帧的 pts

    double audio_clock_;
    double video_clock_;

    // ================== Misc ==================
    SDL_Thread *read_tid_;
    SDL_Thread *decode_tid_;

    SDL_cond *continue_read_thread_;

    bool quit_{false};
};

// ================== PacketQueue Functions ==================
int InitPacketQueue(PacketQueue *q);

int PutPacketQueueInternal(PacketQueue *q, AVPacket *pkt);

int PutPacketQueue(PacketQueue *q, AVPacket *pkt);

int GetPacketQueue(PacketQueue *q, AVPacket *pkt, int block);

void FlushPacketQueue(PacketQueue *q);

void DestoryPacketQueue(PacketQueue *q);

// ================== FrameQueue Functions ==================
int InitFrameQueue(FrameQueue *f, PacketQueue *pktq, int max_size, int keep_last);

void MoveWriteIndex(FrameQueue *f);  // 将写索引后移

void MoveReadIndex(FrameQueue *f);  // 将读索引后移

Frame *PeekWritableFrameQueue(FrameQueue *f);

Frame *PeekFrameQueue(FrameQueue *f);

// ================== const ==================
constexpr int kDefaultWidth = 960;
constexpr int kDefaultHeight = 540;
// constexpr int kScreenLeft = SDL_WINDOWPOS_CENTERED; // 窗口左上角的 x 坐标
// constexpr int kScreenTop = SDL_WINDOWPOS_CENTERED;  // 窗口左上角的 y 坐标
constexpr int kVideoPictureQueueSize = 3;
constexpr int kFFRefreshEvent = SDL_USEREVENT + 1;
constexpr int kMaxQueueSize = 15 * 1024 * 1024;
constexpr int kSdlAudioBufferSize = 1024;
constexpr double kMaxAvSyncThreshold = 0.1;
constexpr double kMinAvSyncThreshold = 0.04;
constexpr double kAvNoSyncThreshold = 10.0;
constexpr int kScreenWidth = 960;
constexpr int kScreenHeight = 540;

// ================== Logging ==================
void InitLogger();

// 日志宏定义，对应 av_log 的级别
#define LOG_ERROR(...) spdlog::error(__VA_ARGS__)
#define LOG_WARN(...) spdlog::warn(__VA_ARGS__)
#define LOG_INFO(...) spdlog::info(__VA_ARGS__)
#define LOG_DEBUG(...) spdlog::debug(__VA_ARGS__)

// ================== common ==================
void RefreshSchedule(AVState *video_state, int delay);

void SetDefaultWindowSize(int width, int height, AVRational sar);

void CalculateDisplayRect(SDL_Rect *rect, int screen_x_left, int screen_y_top, int screen_width,
                          int screen_height, int picture_width, int picture_height,
                          AVRational picture_sar);

// ================== thread ==================

// ================== read thread ==================
AVState *OpenStream(std::string const &file_name);

int ReadThread(void *arg);

// ================== video thread ==================
int DecodeThread(void *arg);

void SdlEventLoop(AVState *video_state);

// ================== audio thread ==================
int OpenAudio(void *opaque, AVChannelLayout *wanted_channel_layout, int wanted_sample_rate);