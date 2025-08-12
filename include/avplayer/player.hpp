#pragma once

#include <atomic>
#include <avplayer/core.hpp>
#include <avplayer/logger.hpp>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

// NOTE: 选择音频时钟作为主时钟:

namespace avplayer {

// ================== Player Class ==================
class Player {
public:
    explicit Player(std::string file_path);

    ~Player();

    // 禁止拷贝和移动
    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;

public:
    // void Run();

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
    // 解码音频帧 (包含更新音频时钟)
    int DecodeAudioFrame();
    // SDL 音频回调
    static void AudioCallbackWrapper(void* userdata, uint8_t* stream, int len);
    // 音频回调
    void AudioCallback(uint8_t* stream, int len);

    // =============== 视频处理 ===============
    // 解码视频帧 (包含更新视频时钟)
    int DecodeVideoFrame();
    // 视频刷新定时器回调
    static uint32_t VideoRefreshTimerWrapper(uint32_t interval, void* opaque);
    // 调度下一帧视频刷新
    void ScheduleNextVideoRefresh(int delay_ms);
    // 视频刷新处理 (包含音视频同步)
    void VideoRefreshHandler();
    // 渲染视频帧
    void RenderVideoFrame();
    // 计算视频显示区域
    void CalculateDisplayRect(SDL_Rect* rect, int window_x, int window_y, int window_width,
                              int window_height, int picture_width, int picture_height,
                              AVRational picture_sar);

    // =============== 时钟同步 ===============
    // 获取主时钟
    double GetMasterClock() const;
    // 获取视频时钟
    double GetVideoClock() const;
    // 更新视频时钟
    double SynchronizeVideo(const AVFrame* frame, double pts);

    // =============== 控制 ===============
    // 切换暂停/播放状态
    void TogglePause();
    // 停止播放
    void Stop();

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
    int window_x_{0};
    int window_y_{0};
    int window_width_{kDefaultWidth};
    int window_height_{kDefaultHeight};

    // 音频状态
    UniqueSwrContext audio_swr_ctx_;     // 音频重采样上下文
    UniqueAVFrame audio_frame_;          // 音频重采样时使用的 AVFrame
    std::vector<uint8_t> audio_buffer_;  // 音频缓冲区
    uint32_t audio_buffer_size_{0};      // 音频缓冲区大小
    uint32_t audio_buffer_index_{0};     // 音频缓冲区索引

    // 音视频同步
    double audio_clock_{0.0};       // 音频时钟 (主时钟)
    double video_clock_{0.0};       // 视频时钟
    double frame_timer_{0.0};       // 用于消除累计误差的高精度视频同步校正时钟
    double last_frame_pts_{0.0};    // 上一帧显示时间戳
    double last_frame_delay_{0.0};  // 上一帧显示延迟
    //
    std::atomic_bool stop_{false};    // 是否停止
    std::atomic_bool paused_{false};  // 是否暂停
};

}  // namespace avplayer