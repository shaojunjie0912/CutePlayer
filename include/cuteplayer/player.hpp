#pragma once

#include <atomic>
#include <cstdint>
#include <cuteplayer/core.hpp>
#include <cuteplayer/logger.hpp>
#include <string>
#include <thread>
#include <vector>

// NOTE: 选择音频时钟作为主时钟:

namespace cuteplayer {

// ================== Player Class ==================
class Player {
public:
    explicit Player(std::string file_path);

    ~Player();

    // 禁止拷贝和移动
    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;

public:
    void Run();

public:
    // =============== 初始化 ===============
    void InitSDL();
    void OpenInputFile();
    void FindStreams();
    void OpenStreamComponent(int stream_index);
    void StartThreads();

    // =============== 线程循环 ===============
    // 读取线程
    void ReadLoop();
    // 视频解码线程
    void VideoDecodeLoop();

    // =============== 音频处理 ===============
    int DecodeAudioFrame();
    static void AudioCallbackWrapper(void* userdata, uint8_t* stream, int len);
    void AudioCallback(uint8_t* stream, int len);

    // =============== 视频处理 ===============
    int DecodeVideoFrame();
    static uint32_t VideoRefreshTimerWrapper(uint32_t interval, void* opaque);
    void ScheduleNextVideoRefresh(int delay_ms);
    void VideoRefreshHandler();
    void RenderVideoFrame();
    double GetVideoClock() const;
    void SetVideoClock(double pts);
    double SynchronizeVideo(const AVFrame* frame, double pts);
    void CalculateDisplayRect(SDL_Rect* rect, int pic_width, int pic_height, AVRational pic_sar);

    // =============== 时钟同步 ===============
    double GetMasterClock() const;

private:
    std::string file_path_;

    // Queues
    PacketQueue video_packet_queue_;
    PacketQueue audio_packet_queue_;
    FrameQueue video_frame_queue_;

    // FFmpeg
    UniqueAVFormatContext format_ctx_;
    AVStream* video_stream_{nullptr};
    AVStream* audio_stream_{nullptr};
    UniqueAVCodecContext video_codec_ctx_;
    UniqueAVCodecContext audio_codec_ctx_;
    int video_stream_idx_{-1};
    int audio_stream_idx_{-1};

    // 线程
    std::jthread read_thread_;
    std::jthread video_decode_thread_;

    // SDL
    UniqueSDLWindow window_;
    UniqueSDLRenderer renderer_;
    UniqueSDLTexture texture_;
    int window_width_{kDefaultWidth};
    int window_height_{kDefaultHeight};

    // 音频状态
    UniqueSwrContext audio_swr_ctx_;     // 音频重采样上下文
    UniqueAVFrame audio_frame_;          // 音频重采样时使用的 AVFrame
    std::vector<uint8_t> audio_buffer_;  // 音频缓冲区
    uint32_t audio_buf_size_{0};         // 音频缓冲区大小
    uint32_t audio_buf_index_{0};        // 音频缓冲区索引

    // 音视频同步
    double audio_clock_{0.0};       // 音频时钟 (主时钟)
    double video_clock_{0.0};       // 视频时钟
    double frame_timer_{0.0};       // 帧定时器
    double frame_last_pts_{0.0};    // 上一帧显示时间戳
    double frame_last_delay_{0.0};  // TODO: 上一帧显示延迟
    //
    std::atomic_bool stop_{false};
};

}  // namespace cuteplayer