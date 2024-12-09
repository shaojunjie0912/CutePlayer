extern "C" {
#include <SDL2/SDL.h>
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
}

#include <condition_variable>
#include <iostream>
#include <mutex>
#include <queue>
#include <thread>

#define SDL_AUDIO_BUFFER_SIZE 1024
#define SCREEN_WIDTH 640
#define SCREEN_HEIGHT 480

// 音频缓冲队列
std::queue<AVFrame *> audio_frame_queue;
std::mutex audio_mutex;
std::condition_variable audio_cond_var;

// 视频缓冲队列
std::queue<AVFrame *> video_frame_queue;
std::mutex video_mutex;
std::condition_variable video_cond_var;

// 音频解码回调
void audio_callback(void *userdata, Uint8 *stream, int len) {
    AVCodecContext *audio_codec_ctx = static_cast<AVCodecContext *>(userdata);
    static uint8_t *audio_buf = nullptr;
    static int audio_buf_size = 0;
    static int audio_buf_index = 0;

    if (audio_buf_index >= audio_buf_size) {
        std::unique_lock<std::mutex> lock(audio_mutex);
        audio_cond_var.wait(lock, [] { return !audio_frame_queue.empty(); });

        AVFrame *frame = audio_frame_queue.front();
        audio_frame_queue.pop();
        lock.unlock();

        int data_size = av_samples_get_buffer_size(
            nullptr, audio_codec_ctx->channels, frame->nb_samples, audio_codec_ctx->sample_fmt, 1);
        audio_buf = frame->data[0];
        audio_buf_size = data_size;
        audio_buf_index = 0;
    }

    int bytes_to_copy = std::min(len, audio_buf_size - audio_buf_index);
    memcpy(stream, audio_buf + audio_buf_index, bytes_to_copy);
    audio_buf_index += bytes_to_copy;
}

// 视频解码线程
void video_decode_thread(AVFormatContext *format_ctx, AVCodecContext *video_codec_ctx,
                         SDL_Renderer *renderer) {
    while (true) {
        AVPacket packet;
        if (av_read_frame(format_ctx, &packet) < 0) {
            break;
        }

        if (packet.stream_index == video_codec_ctx->stream_index) {
            int ret = avcodec_receive_frame(video_codec_ctx, video_codec_ctx->frame);
            if (ret == 0) {
                std::unique_lock<std::mutex> lock(video_mutex);
                video_frame_queue.push(video_codec_ctx->frame);
                video_cond_var.notify_one();
            }
        }

        av_packet_unref(&packet);
    }
}

// 音频解码线程
void audio_decode_thread(AVFormatContext *format_ctx, AVCodecContext *audio_codec_ctx) {
    while (true) {
        AVPacket packet;
        if (av_read_frame(format_ctx, &packet) < 0) {
            break;
        }

        if (packet.stream_index == audio_codec_ctx->) {
            int ret = avcodec_receive_frame(audio_codec_ctx, audio_codec_ctx->frame);
            if (ret == 0) {
                std::unique_lock<std::mutex> lock(audio_mutex);
                audio_frame_queue.push(audio_codec_ctx->frame);
                audio_cond_var.notify_one();
            }
        }

        av_packet_unref(&packet);
    }
}

// 渲染视频线程
void video_render_thread(SDL_Renderer *renderer) {
    while (true) {
        std::unique_lock<std::mutex> lock(video_mutex);
        video_cond_var.wait(lock, [] { return !video_frame_queue.empty(); });

        AVFrame *frame = video_frame_queue.front();
        video_frame_queue.pop();
        lock.unlock();

        SDL_Texture *texture =
            SDL_CreateTexture(renderer, SDL_PIXELFORMAT_YV12, SDL_TEXTUREACCESS_STREAMING,
                              frame->width, frame->height);
        SDL_UpdateYUVTexture(texture, nullptr, frame->data[0], frame->linesize[0], frame->data[1],
                             frame->linesize[1], frame->data[2], frame->linesize[2]);

        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);
        SDL_DestroyTexture(texture);
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <video-file>" << std::endl;
        return -1;
    }

    const char *filename = argv[1];

    // 初始化 FFmpeg
    avformat_network_init();

    AVFormatContext *format_ctx = nullptr;
    if (avformat_open_input(&format_ctx, filename, nullptr, nullptr) < 0) {
        std::cerr << "Couldn't open the file." << std::endl;
        return -1;
    }

    if (avformat_find_stream_info(format_ctx, nullptr) < 0) {
        std::cerr << "Couldn't find stream information." << std::endl;
        return -1;
    }

    // 查找视频和音频流
    AVCodec const *audio_codec = nullptr, *video_codec = nullptr;
    int audio_stream_index = -1, video_stream_index = -1;
    for (int i = 0; i < format_ctx->nb_streams; i++) {
        if (format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO &&
            audio_stream_index == -1) {
            audio_stream_index = i;
            audio_codec = avcodec_find_decoder(format_ctx->streams[i]->codecpar->codec_id);
        }
        if (format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
            video_stream_index == -1) {
            video_stream_index = i;
            video_codec = avcodec_find_decoder(format_ctx->streams[i]->codecpar->codec_id);
        }
    }

    if (!audio_codec || !video_codec) {
        std::cerr << "Couldn't find audio or video codec." << std::endl;
        return -1;
    }

    // 初始化解码器
    AVCodecContext *audio_codec_ctx = avcodec_alloc_context3(audio_codec);
    AVCodecContext *video_codec_ctx = avcodec_alloc_context3(video_codec);

    if (!audio_codec_ctx || !video_codec_ctx) {
        std::cerr << "Failed to allocate codec contexts." << std::endl;
        return -1;
    }

    if (avcodec_parameters_to_context(audio_codec_ctx,
                                      format_ctx->streams[audio_stream_index]->codecpar) < 0 ||
        avcodec_parameters_to_context(video_codec_ctx,
                                      format_ctx->streams[video_stream_index]->codecpar) < 0) {
        std::cerr << "Failed to copy codec parameters." << std::endl;
        return -1;
    }

    if (avcodec_open2(audio_codec_ctx, audio_codec, nullptr) < 0 ||
        avcodec_open2(video_codec_ctx, video_codec, nullptr) < 0) {
        std::cerr << "Failed to open codec." << std::endl;
        return -1;
    }

    // 初始化 SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        std::cerr << "SDL Initialization failed: " << SDL_GetError() << std::endl;
        return -1;
    }

    SDL_Window *window =
        SDL_CreateWindow("FFmpeg + SDL2 Video Player", SDL_WINDOWPOS_UNDEFINED,
                         SDL_WINDOWPOS_UNDEFINED, SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_SHOWN);
    if (!window) {
        std::cerr << "Failed to create window." << std::endl;
        return -1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        std::cerr << "Failed to create renderer." << std::endl;
        return -1;
    }

    SDL_AudioSpec wanted_spec;
    wanted_spec.freq = audio_codec_ctx->sample_rate;
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.channels = audio_codec_ctx->ch_layout.nb_channels;
    wanted_spec.silence = 0;
    wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;
    wanted_spec.callback = audio_callback;
    wanted_spec.userdata = audio_codec_ctx;

    if (SDL_OpenAudio(&wanted_spec, nullptr) < 0) {
        std::cerr << "SDL_OpenAudio failed: " << SDL_GetError() << std::endl;
        return -1;
    }

    SDL_PauseAudio(0);

    // 创建视频解码和音频解码线程
    std::thread video_decoder(video_decode_thread, format_ctx, video_codec_ctx, renderer);
    std::thread audio_decoder(audio_decode_thread, format_ctx, audio_codec_ctx);
    std::thread video_renderer(video_render_thread, renderer);

    // 等待所有线程结束
    video_decoder.join();
    audio_decoder.join();
    video_renderer.join();

    // 清理
    avcodec_free_context(&audio_codec_ctx);
    avcodec_free_context(&video_codec_ctx);
    avformat_close_input(&format_ctx);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
