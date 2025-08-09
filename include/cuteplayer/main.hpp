#pragma once

#include <condition_variable>
#include <mutex>
#include <string>

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

// ================== core ==================
constexpr int kFrameQueueSize = 16;

struct MyAVPacketList {
    AVPacket *pkt;
};

struct PacketQueue {
    AVFifo *pkt_list_; /* ffmpeg封装的队列数据结构，里面的数据对象是MyAVPacketList */
    int nb_packets_;   /* 队列中当前的packet数 */
    int size_;         /* 队列所有节点占用的总内存大小 */
    int64_t duration_; /* 队列中所有节点的合计时长 */
    std::mutex mtx_;
    std::condition_variable cv_;  // 队列是否为空的条件变量
};

struct Frame {
    AVFrame *frame_;
    double pts_;      /* presentation timestamp for the frame */
    double duration_; /* estimated duration of the frame */
    int64_t pos_;     /* byte position of the frame in the input file */
    int width_;
    int height_;
    int format_;
    AVRational sar_;
};

struct FrameQueue {
    Frame queue_[kFrameQueueSize]; /* 用于存放帧数据的队列 */
    int rindex_;                   /* 读索引 */
    int windex_;                   /* 写索引 */
    int size_;                     /* 队列中的帧数 */
    int max_size_;                 /* 队列最大缓存的帧数 */
    int keep_last_;                /* 播放后是否在队列中保留上一帧不销毁 */
    int rindex_shown_; /* keep_last的实现，读的时候实际上读的是rindex + rindex_shown，分析见下 */
    std::mutex mtx_;
    std::condition_variable cv_notfull_;   // 队列是否为空的条件变量
    std::condition_variable cv_notempty_;  // 队列是否为满的条件变量
    PacketQueue *pktq_;                    // 关联的 PacketQueue
};

struct VideoState {
    std::string file_name_;
    AVFormatContext *format_context_;

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

// ================== common ==================
void RefreshSchedule(VideoState *video_state, int delay);

void SetDefaultWindowSize(int width, int height, AVRational sar);

void CalculateDisplayRect(SDL_Rect *rect, int screen_x_left, int screen_y_top, int screen_width,
                          int screen_height, int picture_width, int picture_height,
                          AVRational picture_sar);

// ================== thread ==================

// ================== read thread ==================
VideoState *OpenStream(std::string const &file_name);

int ReadThread(void *arg);

// ================== video thread ==================
int DecodeThread(void *arg);

void SdlEventLoop(VideoState *video_state);

// ================== audio thread ==================
int OpenAudio(void *opaque, AVChannelLayout *wanted_channel_layout, int wanted_sample_rate);