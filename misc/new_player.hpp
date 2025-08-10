#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <cuteplayer/raii_utils.hpp>

namespace cuteplayer {

// 前向声明
class Player;

// ================== Decoded Frame Wrapper ==================
struct DecodedFrame {
    UniqueAVFrame frame;
    double pts{};
    double duration{};
};

// ================== PacketQueue Class ==================
class PacketQueue {
public:
    PacketQueue();
    ~PacketQueue();

    // 禁止拷贝和移动
    PacketQueue(const PacketQueue&) = delete;
    PacketQueue& operator=(const PacketQueue&) = delete;

    bool Put(UniqueAVPacket packet);
    std::optional<UniqueAVPacket> Get(bool block, std::stop_token stop_token);
    void Flush();
    int GetSize() const;

private:
    AVFifo* fifo_{nullptr};
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    int packet_count_{0};
    int total_size_{0};
};


// ================== FrameQueue Class ==================
class FrameQueue {
public:
    explicit FrameQueue(int max_size);

    // 禁止拷贝和移动
    FrameQueue(const FrameQueue&) = delete;
    FrameQueue& operator=(const FrameQueue&) = delete;

    std::optional<DecodedFrame*> PeekWritable();
    void Push();
    std::optional<const DecodedFrame*> PeekReadable() const;
    void Pop();
    int GetSize() const;

private:
    std::deque<DecodedFrame> queue_;
    int max_size_;
    mutable std::mutex mutex_;
    std::condition_variable cv_not_full_;
    std::condition_variable cv_not_empty_;
};


// ================== Player Class ==================
class Player {
public:
    explicit Player(std::string file_path);
    ~Player();

    // 禁止拷贝和移动
    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;

    void RunEventLoop();

private:
    // ================== Initialization ==================
    void init_sdl();
    void open_input_file();
    void find_streams();
    void open_stream_component(int stream_index);
    void start_threads();

    // ================== Thread Loops ==================
    void read_loop(std::stop_token stop_token);
    void video_decode_loop(std::stop_token stop_token);

    // ================== Audio Handling ==================
    static void audio_callback_wrapper(void* userdata, Uint8* stream, int len);
    void audio_callback(Uint8* stream, int len);
    int decode_audio_frame();

    // ================== Video Handling ==================
    static uint32_t video_refresh_timer_wrapper(uint32_t interval, void* opaque);
    void schedule_next_video_refresh(int delay_ms);
    void video_refresh_handler();
    void render_video_frame();
    double get_video_clock() const;
    void set_video_clock(double pts);
    double synchronize_video(const AVFrame* frame, double pts);
    void calculate_display_rect(SDL_Rect* rect, int pic_width, int pic_height, AVRational pic_sar);


    // ================== Sync Clock ==================
    double get_master_clock() const;

    // ================== Core Members ==================
    std::string file_path_;
    std::stop_source stop_source_;

    // FFmpeg
    UniqueAVFormatContext format_ctx_;
    AVStream* video_stream_{nullptr};
    AVStream* audio_stream_{nullptr};
    UniqueAVCodecContext video_codec_ctx_;
    UniqueAVCodecContext audio_codec_ctx_;
    int video_stream_idx_{-1};
    int audio_stream_idx_{-1};

    // Queues
    PacketQueue video_packet_queue_;
    PacketQueue audio_packet_queue_;
    FrameQueue video_frame_queue_;
    
    // SDL
    UniqueSDLWindow window_;
    UniqueSDLRenderer renderer_;
    UniqueSDLTexture texture_;
    int window_width_{kDefaultWidth};
    int window_height_{kDefaultHeight};

    // Threads
    std::jthread read_thread_;
    std::jthread video_decode_thread_;

    // Audio State
    UniqueSwrContext swr_ctx_;
    std::vector<uint8_t> audio_buffer_;
    unsigned int audio_buffer_size_{0};
    unsigned int audio_buffer_index_{0};
    
    // Clock & Sync
    double audio_clock_{0.0};
    double video_clock_{0.0};
    double frame_timer_{0.0};
    double frame_last_pts_{0.0};
    double frame_last_delay_{0.04}; // Default to 40ms
};

} // namespace cuteplayer