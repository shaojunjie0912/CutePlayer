#pragma once

extern "C" {
#include <SDL2/SDL.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
#include <libavutil/mem.h>
#include <libavutil/time.h>
#include <libswresample/swresample.h>
#define SDL_MAIN_HANDLED
}

#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <vector>

namespace avplayer {

// ================== Constants ==================

constexpr int kDefaultWidth = 1920;                         // SDL 窗口默认宽度
constexpr int kDefaultHeight = 1080;                        // SDL 窗口默认高度
constexpr int kMaxFrameQueueSize = 3;                       // 视频帧环形队列大小
constexpr int kMaxPacketQueueDataBytes = 15 * 1024 * 1024;  // 15 MB
constexpr int kSdlAudioBufferSize = 1024;                   // SDL 音频缓冲区每次填充的字节数
constexpr double kMaxAvSyncThreshold = 0.100;               // 100ms
constexpr double kMinAvSyncThreshold = 0.040;               // 40ms
constexpr double kAvNoSyncThreshold = 10.0;                 // 10s (严重到没必要同步)
constexpr int kFFRefreshEvent = SDL_USEREVENT + 1;

// ================== FFmpeg Deleters ==================

struct AVFormatContextDeleter {
    void operator()(AVFormatContext* p) const {
        if (p) {
            avformat_close_input(&p);
        }
    }
};

struct AVCodecContextDeleter {
    void operator()(AVCodecContext* p) const {
        if (p) {
            avcodec_free_context(&p);
        }
    }
};

struct AVFrameDeleter {
    void operator()(AVFrame* p) const {
        if (p) {
            av_frame_free(&p);
        }
    }
};

struct AVPacketDeleter {
    void operator()(AVPacket* p) const {
        if (p) {
            av_packet_free(&p);
        }
    }
};

struct SwrContextDeleter {
    void operator()(SwrContext* p) const {
        if (p) {
            swr_free(&p);
        }
    }
};

// ================== FFmpeg unique_ptr Aliases ==================

using UniqueAVFormatContext = std::unique_ptr<AVFormatContext, AVFormatContextDeleter>;
using UniqueAVCodecContext = std::unique_ptr<AVCodecContext, AVCodecContextDeleter>;
using UniqueAVFrame = std::unique_ptr<AVFrame, AVFrameDeleter>;
using UniqueAVPacket = std::unique_ptr<AVPacket, AVPacketDeleter>;
using UniqueSwrContext = std::unique_ptr<SwrContext, SwrContextDeleter>;

// ================== SDL Deleters ==================

struct SDLWindowDeleter {
    void operator()(SDL_Window* p) const {
        if (p) {
            SDL_DestroyWindow(p);
        }
    }
};

struct SDLRendererDeleter {
    void operator()(SDL_Renderer* p) const {
        if (p) {
            SDL_DestroyRenderer(p);
        }
    }
};

struct SDLTextureDeleter {
    void operator()(SDL_Texture* p) const {
        if (p) {
            SDL_DestroyTexture(p);
        }
    }
};

// ================== SDL unique_ptr Aliases ==================

using UniqueSDLWindow = std::unique_ptr<SDL_Window, SDLWindowDeleter>;
using UniqueSDLRenderer = std::unique_ptr<SDL_Renderer, SDLRendererDeleter>;
using UniqueSDLTexture = std::unique_ptr<SDL_Texture, SDLTextureDeleter>;

// ================== PacketQueue Class ==================
class PacketQueue {
public:
    PacketQueue(std::size_t max_data_bytes) : max_data_bytes_(max_data_bytes) {}
    ~PacketQueue() = default;
    PacketQueue(const PacketQueue&) = delete;
    PacketQueue(PacketQueue&&) = delete;

public:
    // Push (阻塞)
    bool Push(UniqueAVPacket packet);

    // Pop (阻塞)
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
    std::size_t max_data_bytes_{0};     // 最大总字节大小
    int64_t duration_{0};               // 总时长
    mutable std::mutex mtx_;
    std::condition_variable cv_can_pop_;   // 能 pop 的条件变量
    std::condition_variable cv_can_push_;  // 能 push 的条件变量
    bool closed_{false};                   // 队列是否已关闭
};

// ================== Decoded Frame Wrapper ==================
struct DecodedFrame {
    UniqueAVFrame frame_;  // 解码后的 AVFrame 指针 (unique_ptr)
    double pts_{};         // 帧的显示时间戳
    double duration_{};    // 帧的估计持续时间
    int64_t pos_{};        // 帧在输入文件或流中的字节位置 (NOTE: 精确跳转 (Seek) 快进快退)
    int width_{};          // 帧的宽度
    int height_{};         // 帧的高度
    int format_{};         // 帧的像素格式
    AVRational sar_{};     // 帧的宽高比
};

// ================== FrameQueue Class ==================
class FrameQueue {
public:
    explicit FrameQueue(int max_size);

    ~FrameQueue() = default;

    FrameQueue(const FrameQueue&) = delete;  // 禁止拷贝

    FrameQueue(FrameQueue&&) = delete;  // 禁止移动

public:
    // 获取当前可写 Frame 指针 (阻塞)
    DecodedFrame* PeekWritable();

    // 移动写入索引
    void MoveWriteIndex();

    // 获取当前可读 Frame 指针 (阻塞)
    DecodedFrame* PeekReadable();

    // 获取上一帧
    DecodedFrame* PeekLastReadable();

    // 移动读取索引
    void MoveReadIndex();

    std::size_t GetSize() const;

    // 清空队列
    void Clear();

    // 关闭队列
    void Close();

private:
    size_t rindex_{0};    // 读取索引
    size_t windex_{0};    // 写入索引
    size_t size_{0};      // 当前帧数
    size_t max_size_{0};  // 最大帧数

    std::vector<DecodedFrame> decoded_frames_;  // 解码帧环形队列
    std::condition_variable cv_can_write_;
    std::condition_variable cv_can_read_;
    mutable std::mutex mtx_;
    bool closed_{false};  // 队列是否已关闭
};

}  // namespace avplayer