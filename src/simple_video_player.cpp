#include <cstdint>
#include <cstdio>

// TODO: FILE 改为 C++ 的 fstream

extern "C" {
#include <SDL2/SDL.h>
#include <SDL2/SDL_thread.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
};

int main(int argc, char *argv[]) {
    char const *input_file = nullptr;
    int ret = -1;
    int max_frames_decode;

#ifdef MP4_FILE
    input_file = MP4_FILE;
#else
    printf("Please provide an input file.\n");
    return -1
#endif
    max_frames_decode = 10000;

    ret = SDL_Init(SDL_INIT_EVERYTHING);
    if (ret != 0) {
        printf("Could not initialize SDL - %s\n.", SDL_GetError());
        return -1;
    }

    AVFormatContext *format_context = avformat_alloc_context();
    ret = avformat_open_input(&format_context, input_file, nullptr, nullptr);
    if (ret < 0) {
        printf("Could not open input file.\n");
        return -1;
    }
    ret = avformat_find_stream_info(format_context, nullptr);
    if (ret < 0) {
        printf(
            "Could not find stream "
            "information.\n");
        return -1;
    }

    AVCodecParameters *codec_params = nullptr;  // 被编码流的各种属性
    AVCodec const *codec = nullptr;             // 编解码器信息
    int video_stream_index = -1;                // 视频流索引

    for (int i = 0; i < format_context->nb_streams; ++i) {
        AVCodecParameters *local_codec_params = format_context->streams[i]->codecpar;
        AVCodec const *local_codec = avcodec_find_decoder(local_codec_params->codec_id);
        if (!local_codec) {
            printf("Unsupported codec!\n");
            return -1;
        }
        if (local_codec_params->codec_type == AVMEDIA_TYPE_VIDEO) {
            printf(
                "[视频] 编解码器: %s; 分辨率: "
                "%dx%d\n",
                local_codec->name, local_codec_params->width, local_codec_params->height);
            codec_params = local_codec_params;
            codec = local_codec;
            video_stream_index = i;
        } else if (local_codec_params->codec_type == AVMEDIA_TYPE_AUDIO) {
            printf(
                "[音频] 编解码器: %s; 通道数: "
                "%d, 采样率: %dHz\n",
                local_codec->name, local_codec_params->ch_layout.nb_channels,
                local_codec_params->sample_rate);
        }
    }
    AVCodecContext *codec_context = avcodec_alloc_context3(codec);
    ret = avcodec_parameters_to_context(codec_context, codec_params);
    if (ret < 0) {
        printf("Could not copy codec context.\n");
        return -1;
    }
    ret = avcodec_open2(codec_context, codec, nullptr);
    if (ret < 0) {
        printf("Could not open codec.\n");
        return -1;
    }

    AVFrame *frame = av_frame_alloc();
    AVFrame *frame_yuv = av_frame_alloc();

    SDL_Window *window =
        SDL_CreateWindow("SDL Video Player", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                         codec_context->width / 2, codec_context->height / 2,
                         SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) {
        printf(
            "SDL: could not set video mode - "
            "exiting.\n");
        return -1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(
        window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE);

    SDL_Texture *texture =
        SDL_CreateTexture(renderer, SDL_PIXELFORMAT_YV12, SDL_TEXTUREACCESS_STREAMING,
                          codec_context->width, codec_context->height);

    SwsContext *img_convert_context = sws_getContext(
        codec_context->width, codec_context->height, codec_context->pix_fmt, codec_context->width,
        codec_context->height, AV_PIX_FMT_YUV420P, SWS_BILINEAR, nullptr, nullptr, nullptr);

    uint8_t *buffer =
        (uint8_t *)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P, codec_context->width,
                                                      codec_context->height, 32) *
                             sizeof(uint8_t));
    av_image_fill_arrays(frame_yuv->data, frame_yuv->linesize, buffer, AV_PIX_FMT_YUV420P,
                         codec_context->width, codec_context->height, 32);

    AVPacket *packet = av_packet_alloc();
    int i = 0;  //
    while (av_read_frame(format_context, packet) >= 0) {
        if (packet->stream_index == video_stream_index) {
            int ret = avcodec_send_packet(codec_context, packet);
            if (ret < 0) {
                printf(
                    "Error sending packet for "
                    "decoding.\n");
                return -1;
            }
            while (ret >= 0) {
                ret = avcodec_receive_frame(codec_context, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    // EOF exit loop
                    break;
                } else if (ret < 0) {
                    // could not decode packet
                    printf(
                        "Error while "
                        "decoding.\n");
                    // exit with error
                    return -1;
                }
                // NOTE: 解码出来的 Frame
                // 有黑边，因此需要对其进行缩放
                // TODO: 这里的转换没看懂
                sws_scale(img_convert_context, (uint8_t const *const *)frame->data, frame->linesize,
                          0, codec_context->height, frame_yuv->data, frame_yuv->linesize);
                if (++i <= max_frames_decode) {
                    double fps = av_q2d(format_context->streams[video_stream_index]->r_frame_rate);
                    double sleep_time = 1.0 / fps;
                    SDL_Delay((1000 * sleep_time) - 10);

                    SDL_Rect rect{
                        .x = 0, .y = 0, .w = codec_context->width, .h = codec_context->height};
                    printf(
                        "Frame %c (%lld), "
                        "[%dx%d]\n",
                        av_get_picture_type_char(frame->pict_type), codec_context->frame_num,
                        codec_context->width, codec_context->height);
                    SDL_UpdateYUVTexture(texture, &rect, frame_yuv->data[0], frame_yuv->linesize[0],
                                         frame_yuv->data[1], frame_yuv->linesize[1],
                                         frame_yuv->data[2], frame_yuv->linesize[2]);
                    SDL_RenderClear(renderer);
                    SDL_RenderCopy(renderer, texture, nullptr, nullptr);
                    SDL_RenderPresent(renderer);
                } else {
                    break;
                }
            }
            if (i > max_frames_decode) {
                break;
            }
        }
        av_packet_unref(packet);

        SDL_Event event;
        SDL_PollEvent(&event);
        switch (event.type) {
            case SDL_QUIT: {
                SDL_Quit();
                exit(0);
            } break;

            default: {
                // nothing to do
            } break;
        }
    }
    av_frame_free(&frame);
    av_frame_free(&frame_yuv);
    sws_freeContext(img_convert_context);
    avformat_close_input(&format_context);
    av_packet_free(&packet);
    avcodec_free_context(&codec_context);

    SDL_DestroyRenderer(renderer);
    SDL_Quit();
    return 0;
}