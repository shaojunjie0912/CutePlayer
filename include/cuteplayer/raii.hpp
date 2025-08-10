#pragma once

#include <spdlog/spdlog.h>

#include <memory>

extern "C" {
#include <SDL2/SDL.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
#include <libavutil/mem.h>
#include <libswresample/swresample.h>
}

namespace cuteplayer {

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

}  // namespace cuteplayer