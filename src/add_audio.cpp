// ref:
// https://github.com/rambodrahmani/ffmpeg-video-player/blob/master/tutorial03/tutorial03-resampled.c
#include <SDL2/SDL_audio.h>
#include <SDL2/SDL_events.h>
#include <libavcodec/packet.h>

#include <cstdint>
#include <cstdio>
#include <string>

// TODO: FILE 改为 C++ 的 fstream
// TODO: 使用现代 C++ 的智能指针
// TODO: 使用现代 C++ 的多线程

extern "C" {
#include <SDL2/SDL.h>
#include <SDL2/SDL_thread.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
};

#define SDL_AUDIO_BUFFER_SIZE 1024
#define MAX_AUDIO_FRAME_SIZE 192000

struct PacketQueue {
    AVPacketList* first_pkt;
    AVPacketList* last_pkt;
    int nb_packets;
    int size;
    SDL_mutex* mutex;
    SDL_cond* cond;
};

PacketQueue audio_queue;

int quit = 0;

void AudioCallback(void*, uint8_t*, int);

void InitPacketQueue(PacketQueue* q) {
    // alloc memory for the audio queue
    memset(q, 0, sizeof(PacketQueue));

    // Returns the initialized and unlocked mutex or NULL on failure
    q->mutex = SDL_CreateMutex();
    if (!q->mutex) {
        // could not create mutex
        printf("SDL_CreateMutex Error: %s.\n", SDL_GetError());
        return;
    }

    // Returns a new condition variable or NULL on failure
    q->cond = SDL_CreateCond();
    if (!q->cond) {
        // could not create condition variable
        printf("SDL_CreateCond Error: %s.\n", SDL_GetError());
        return;
    }
}

int PushToPacketQueue(PacketQueue* q, AVPacket* pkt) {
    AVPacketList* avPacketList = (AVPacketList*)av_malloc(sizeof(AVPacketList));

    // check the AVPacketList was allocated
    if (!avPacketList) {
        return -1;
    }

    // add reference to the given AVPacket
    avPacketList->pkt = *pkt;

    // the new AVPacketList will be inserted at the end of the queue
    avPacketList->next = NULL;

    // lock mutex
    SDL_LockMutex(q->mutex);

    // check the queue is empty
    if (!q->last_pkt) {
        // if it is, insert as first
        q->first_pkt = avPacketList;
    } else {
        // if not, insert as last
        q->last_pkt->next = avPacketList;
    }

    // point the last AVPacketList in the queue to the newly created AVPacketList
    q->last_pkt = avPacketList;

    // increase by 1 the number of AVPackets in the queue
    q->nb_packets++;

    // increase queue size by adding the size of the newly inserted AVPacket
    q->size += avPacketList->pkt.size;

    // notify packet_queue_get which is waiting that a new packet is available
    SDL_CondSignal(q->cond);

    // unlock mutex
    SDL_UnlockMutex(q->mutex);

    return 0;
}

int main(int argc, char* argv[]) {
    char const* input_file = nullptr;
    int ret = -1;
    int max_frames_decode;

    if (argc < 2) {
        printf("Usage: ./build/src/main <input_file_path> <max_frames_decode>\n");
        return -1;
    } else if (argc >= 2) {
        input_file = argv[1];
        max_frames_decode = std::stoi(argv[2]);
    }

    ret = SDL_Init(SDL_INIT_EVERYTHING);
    if (ret != 0) {
        printf("Could not initialize SDL - %s\n.", SDL_GetError());
        return -1;
    }

    AVFormatContext* format_context = avformat_alloc_context();
    avformat_open_input(&format_context, input_file, nullptr, nullptr);
    avformat_find_stream_info(format_context, nullptr);

    int video_stream_index = -1;  // 视频流索引
    int audio_stream_index = -1;  // 音频流索引
    for (int i = 0; i < format_context->nb_streams; ++i) {
        if (format_context->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = i;
        }
        if (format_context->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_stream_index = i;
        }
    }
    if (video_stream_index == -1) {
        printf("No video stream found.\n");
        return -1;
    }
    if (audio_stream_index == -1) {
        printf("No audio stream found.\n");
        return -1;
    }

    AVCodecParameters* audio_codec_params = format_context->streams[audio_stream_index]->codecpar;
    AVCodec const* audio_codec = avcodec_find_decoder(audio_codec_params->codec_id);
    if (!audio_codec) {
        printf("Unsupported codec!\n");
        return -1;
    }

    AVCodecContext* audio_codec_context = avcodec_alloc_context3(audio_codec);
    ret = avcodec_parameters_to_context(audio_codec_context, audio_codec_params);
    if (ret < 0) {
        printf("Could not copy codec context.\n");
        return -1;
    }

    uint8_t tmp_channels = audio_codec_context->ch_layout.nb_channels;  // HACK: int->uint
    SDL_AudioSpec wanted_specs{.freq = audio_codec_context->sample_rate,
                               .format = AUDIO_S16SYS,
                               .channels = tmp_channels,
                               .silence = 0,
                               .samples = SDL_AUDIO_BUFFER_SIZE,
                               .callback = AudioCallback,
                               .userdata = audio_codec_context};
    SDL_AudioSpec specs;
    SDL_AudioDeviceID audio_device_id =
        SDL_OpenAudioDevice(nullptr, 0, &wanted_specs, &specs, SDL_AUDIO_ALLOW_FORMAT_CHANGE);

    if (audio_device_id == 0) {
        printf("Failed to open audio device: %s.\n", SDL_GetError());
        return -1;
    }

    InitPacketQueue(&audio_queue);             // 初始化音频队列
    SDL_PauseAudioDevice(audio_device_id, 0);  // 在音频设备上播放音频

    AVCodecParameters* video_codec_params = format_context->streams[video_stream_index]->codecpar;
    AVCodec const* video_codec = avcodec_find_decoder(video_codec_params->codec_id);
    if (!video_codec) {
        printf("Unsupported codec!\n");
        return -1;
    }

    AVCodecContext* video_codec_context = avcodec_alloc_context3(video_codec);
    ret = avcodec_parameters_to_context(video_codec_context, video_codec_params);
    if (ret < 0) {
        printf("Could not copy codec context.\n");
        return -1;
    }
    ret = avcodec_open2(video_codec_context, video_codec, nullptr);
    if (ret < 0) {
        printf("Could not open codec.\n");
        return -1;
    }

    AVFrame* frame = av_frame_alloc();      // 原始帧
    AVFrame* frame_yuv = av_frame_alloc();  // 转换后的帧

    SDL_Window* window =
        SDL_CreateWindow("SDL Video Player", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                         video_codec_context->width / 2, video_codec_context->height / 2,
                         SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) {
        printf("SDL: could not set video mode - exiting.\n");
        return -1;
    }

    SDL_GL_SetSwapInterval(1);  // HACK: 不知道有啥用

    SDL_Renderer* renderer = SDL_CreateRenderer(
        window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE);

    // HACK: 为什么是 YV12 格式
    SDL_Texture* texture =
        SDL_CreateTexture(renderer, SDL_PIXELFORMAT_YV12, SDL_TEXTUREACCESS_STREAMING,
                          video_codec_context->width, video_codec_context->height);

    SwsContext* img_convert_context = sws_getContext(
        video_codec_context->width, video_codec_context->height, video_codec_context->pix_fmt,
        video_codec_context->width, video_codec_context->height, AV_PIX_FMT_YUV420P, SWS_BILINEAR,
        nullptr, nullptr, nullptr);

    uint8_t* buffer =
        (uint8_t*)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P, video_codec_context->width,
                                                     video_codec_context->height, 32) *
                            sizeof(uint8_t));
    av_image_fill_arrays(frame_yuv->data, frame_yuv->linesize, buffer, AV_PIX_FMT_YUV420P,
                         video_codec_context->width, video_codec_context->height, 32);

    int frame_idx = 0;  // 当前帧索引
    AVPacket* packet = av_packet_alloc();
    while (av_read_frame(format_context, packet) >= 0) {
        // video
        if (packet->stream_index == video_stream_index) {
            int ret = avcodec_send_packet(video_codec_context, packet);
            if (ret < 0) {
                printf("Error sending packet for decoding.\n");
                return -1;
            }
            while (ret >= 0) {
                ret = avcodec_receive_frame(video_codec_context, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    // EOF exit loop
                    break;
                } else if (ret < 0) {
                    // could not decode packet
                    printf("Error while decoding.\n");
                    // exit with error
                    return -1;
                }
                // NOTE: 解码出来的 Frame 有黑边，因此需要对其进行缩放
                // TODO: 这里的转换没看懂
                sws_scale(img_convert_context, (uint8_t const* const*)frame->data, frame->linesize,
                          0, video_codec_context->height, frame_yuv->data, frame_yuv->linesize);

                if (frame_idx++ <= max_frames_decode) {
                    printf("Frame %c (%ld), [%dx%d]\n", av_get_picture_type_char(frame->pict_type),
                           video_codec_context->frame_num, video_codec_context->width,
                           video_codec_context->height);

                    SDL_Rect rect{.x = 0,
                                  .y = 0,
                                  .w = video_codec_context->width,
                                  .h = video_codec_context->height};

                    SDL_UpdateYUVTexture(texture, &rect, frame_yuv->data[0], frame_yuv->linesize[0],
                                         frame_yuv->data[1], frame_yuv->linesize[1],
                                         frame_yuv->data[2], frame_yuv->linesize[2]);
                    SDL_RenderClear(renderer);
                    SDL_RenderCopy(renderer, texture, nullptr, nullptr);
                    SDL_RenderPresent(renderer);
                }
            }
            if (frame_idx > max_frames_decode) {
                printf("Max number of frames to decode processed. Quitting.\n");
                break;
            }
        }
        // audio
        else if (packet->stream_index == audio_stream_index) {
            PushToPacketQueue(&audio_queue, packet);
        }
        // other
        else {
            av_packet_unref(packet);
        }

        SDL_Event event;
        SDL_PollEvent(&event);
        switch (event.type) {
            case SDL_QUIT: {
                printf("SDL_QUIT event received. Quitting.\n");
                SDL_Quit();
                quit = 1;  // 设置全局标志位
            } break;

            default: {
                // nothing to do
            } break;
        }
        if (quit) {
            break;
        }
    }

    av_frame_free(&frame);
    av_frame_free(&frame_yuv);
    sws_freeContext(img_convert_context);
    avformat_close_input(&format_context);
    av_packet_free(&packet);
    avcodec_free_context(&video_codec_context);
    avcodec_free_context(&audio_codec_context);

    SDL_DestroyRenderer(renderer);
    SDL_Quit();

    return 0;
}
