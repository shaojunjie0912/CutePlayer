#pragma once

#include <memory>

extern "C" {
// ================== FFmpeg ==================
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/fifo.h>
#include <libswresample/swresample.h>
// ================== SDL ==================
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
}

namespace cuteplayer {

// 自定义 Deleter for FFmpeg/SDL C-style objects
namespace Deleter {
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
struct AVPacketDeleter {
    void operator()(AVPacket* p) const {
        if (p) {
            av_packet_free(&p);
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
struct AVFifoDeleter {
    void operator()(AVFifo* p) const {
        if (p) {
            av_fifo_freep2(&p);
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
}  // namespace Deleter

// 使用 std::unique_ptr 和自定义 Deleter 为资源创建别名
using UniqueAVFormatContext = std::unique_ptr<AVFormatContext, Deleter::AVFormatContextDeleter>;
using UniqueAVCodecContext = std::unique_ptr<AVCodecContext, Deleter::AVCodecContextDeleter>;
using UniqueAVPacket = std::unique_ptr<AVPacket, Deleter::AVPacketDeleter>;
using UniqueAVFrame = std::unique_ptr<AVFrame, Deleter::AVFrameDeleter>;
using UniqueAVFifo = std::unique_ptr<AVFifo, Deleter::AVFifoDeleter>;
using UniqueSwrContext = std::unique_ptr<SwrContext, Deleter::SwrContextDeleter>;
using UniqueSDLWindow = std::unique_ptr<SDL_Window, Deleter::SDLWindowDeleter>;
using UniqueSDLRenderer = std::unique_ptr<SDL_Renderer, Deleter::SDLRendererDeleter>;
using UniqueSDLTexture = std::unique_ptr<SDL_Texture, Deleter::SDLTextureDeleter>;

}  // namespace cuteplayer