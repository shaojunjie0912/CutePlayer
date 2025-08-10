#pragma once

#include <atomic>
#include <cstdint>
#include <cuteplayer/main.hpp>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <vector>

// NOTE: 选择音频时钟作为主时钟:

namespace cuteplayer {

// ================== PacketQueue Class ==================
class PacketQueue {
public:
    explicit PacketQueue(std::size_t data_size_limit);

    ~PacketQueue() = default;

    // 禁止拷贝
    PacketQueue(const PacketQueue&) = delete;
    // 禁止移动
    PacketQueue(PacketQueue&&) = delete;

public:
    // 阻塞 Push
    bool Push(UniqueAVPacket packet);

    // 阻塞 Pop
    std::optional<UniqueAVPacket> Pop();

    // 非阻塞 Pop
    std::optional<UniqueAVPacket> TryPop();

public:
    // 清空队列
    void Clear();

    // 关闭队列
    void Close();

    // 获取当前总字节大小
    std::size_t GetTotalDataSize() const;

private:
    std::queue<UniqueAVPacket> queue_;  // 队列
    std::size_t curr_data_bytes_{0};    // 当前总字节大小

    // (TODO: 「背压」机制) 防止生产者过快生产数据
    std::size_t max_data_bytes_;  // 基于字节大小的限制

    mutable std::mutex mtx_;
    std::condition_variable cv_can_push_;
    std::condition_variable cv_can_pop_;

    bool closed_{false};  // 队列是否已关闭
};

// ================== Decoded Frame Wrapper ==================
struct DecodedFrame {
    UniqueAVFrame frame;
    double pts{};       // 帧的显示时间戳
    double duration{};  // 帧的估计持续时间
    int64_t pos{};      // 帧在输入文件中的字节位置
    int width{};
    int height{};
    AVRational sar{};
};

// ================== FrameQueue Class ==================
class FrameQueue {
public:
    explicit FrameQueue(std::size_t max_size, bool keep_last_frame);

    ~FrameQueue() = default;

    FrameQueue(const FrameQueue&) = delete;  // 禁止拷贝

    FrameQueue(FrameQueue&&) = delete;  // 禁止移动

public:
    // 获取当前可写 Frame 指针 (阻塞)
    DecodedFrame* PeekWritable();

    // 获取当前可读 Frame 指针 (阻塞)
    DecodedFrame* PeekReadable();

    // 获取当前可读 Frame 指针 (不阻塞)
    DecodedFrame* Peek();

    void Pop();

    void Push();

    std::size_t GetSize() const;

private:
    size_t rindex_{0};                          // 读取索引
    size_t windex_{0};                          // 写入索引
    size_t size_{0};                            // 当前帧数
    size_t max_size_{0};                        // 最大帧数
    size_t rindex_shown_{0};                    // 已显示的帧数
    std::vector<DecodedFrame> decoded_frames_;  // 帧环形缓冲区 (数组)
    std::condition_variable cv_can_push_;
    std::condition_variable cv_can_pop_;
    mutable std::mutex mtx_;
    bool keep_last_frame_{false};  // 是否保留最后一帧
};

// ================== Player Class ==================
class Player {
public:
    explicit Player(std::string file_path);

    ~Player();

    // 禁止拷贝和移动
    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;

    void Run();

public:
    void InitSDL();
    void OpenInputFile();
    void FindStreams();
    void OpenStreamComponent(int stream_index);

    void ReadLoop();
    void VideoDecodeLoop();

    static void AudioCallbackWrapper(void* userdata, uint8_t* stream, int len);
    void AudioCallback(uint8_t* stream, int len);

    int DecodeAudioFrame();

    void StartThreads();

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
    std::atomic_bool running_;
};

}  // namespace cuteplayer