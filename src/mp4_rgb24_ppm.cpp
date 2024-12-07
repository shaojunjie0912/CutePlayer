#include <cstdint>
#include <cstdio>
#include <string>

// TODO: FILE 改为 C++ 的 fstream

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
};

void SaveFrame(AVFrame* frame, int width, int height, int frame_idx) {
    char file_name[100];
    sprintf(file_name, "/home/shaojunjie/Projects/CutePlayer/data/ouput/ppm/frame%d.ppm",
            frame_idx);

    FILE* fp = fopen(file_name, "wb+");
    if (!fp) {
        return;
    }

    // 向 ppm 文件写入文件头
    fprintf(fp, "P6\n%d %d\n255\n", width, height);

    // 写 ppm 文件写入像素数据
    // ✔️  考虑内存对齐，逐行写入
    for (int y = 0; y < height; ++y) {
        // HACK: RGB24 格式下，所有像素的颜色数据存储在一个平面中
        fwrite(frame->data[0] + y * frame->linesize[0], 1, width * 3, fp);
    }
    // fwrite(frame->data[0], 1, height * width * 3, fp);  // ❌ 不对 未考虑内存对齐

    fclose(fp);
}

int main(int argc, char* argv[]) {
    char const* input_file = nullptr;
    char const* output_file = nullptr;
    int ret;
    int max_frames_decode;
    if (argc < 2) {
        printf("Usage: ./build/src/main <input_file_path> <max_frames_decode>\n");
        return -1;
    } else if (argc >= 2) {
        input_file = argv[1];
        max_frames_decode = std::stoi(argv[2]);
    }

    AVFormatContext* format_context = avformat_alloc_context();

    avformat_open_input(&format_context, input_file, nullptr, nullptr);

    avformat_find_stream_info(format_context, nullptr);

    AVCodecParameters* codec_params = nullptr;  // 被编码流的各种属性
    AVCodec const* codec = nullptr;             // 编解码器信息
    int video_stream_index = -1;                // 视频流索引

    for (int i = 0; i < format_context->nb_streams; ++i) {
        AVCodecParameters* local_codec_params = format_context->streams[i]->codecpar;
        AVCodec const* local_codec = avcodec_find_decoder(local_codec_params->codec_id);
        if (!local_codec) {
            printf("Unsupported codec!\n");
            return -1;
        }
        if (local_codec_params->codec_type == AVMEDIA_TYPE_VIDEO) {
            printf("[视频] 编解码器: %s; 分辨率: %dx%d\n", local_codec->name,
                   local_codec_params->width, local_codec_params->height);
            codec_params = local_codec_params;
            codec = local_codec;
            video_stream_index = i;
        } else if (local_codec_params->codec_type == AVMEDIA_TYPE_AUDIO) {
            printf("[音频] 编解码器: %s; 通道数: %d, 采样率: %dHz\n", local_codec->name,
                   local_codec_params->ch_layout.nb_channels, local_codec_params->sample_rate);
        }
    }
    AVCodecContext* codec_context = avcodec_alloc_context3(codec);
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

    AVFrame* frame = av_frame_alloc();
    AVFrame* frame_rgb = av_frame_alloc();

    // TODO: why alian = 32?
    uint8_t* buffer =
        (uint8_t*)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_RGB24, codec_context->width,
                                                     codec_context->height, 32) *
                            sizeof(uint8_t));
    av_image_fill_arrays(frame_rgb->data, frame_rgb->linesize, buffer, AV_PIX_FMT_RGB24,
                         codec_context->width, codec_context->height, 32);

    // 开始从流中读取帧数据
    SwsContext* img_convert_context = sws_getContext(
        codec_context->width, codec_context->height, codec_context->pix_fmt, codec_context->width,
        codec_context->height, AV_PIX_FMT_RGB24, SWS_BILINEAR, nullptr, nullptr, nullptr);

    AVPacket* packet = av_packet_alloc();
    int i = 0;  //
    while (av_read_frame(format_context, packet) >= 0) {
        if (packet->stream_index == video_stream_index) {
            int ret = avcodec_send_packet(codec_context, packet);
            if (ret < 0) {
                printf("Error sending packet for decoding.\n");
                return -1;
            }
            while (ret >= 0) {
                ret = avcodec_receive_frame(codec_context, frame);
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
                          0, codec_context->height, frame_rgb->data, frame_rgb->linesize);
                if (++i <= max_frames_decode) {
                    SaveFrame(frame_rgb, codec_context->width, codec_context->height, i);
                    printf("Frame %c (%ld), [%dx%d]\n", av_get_picture_type_char(frame->pict_type),
                           codec_context->frame_num, codec_context->width, codec_context->height);
                } else {
                    break;
                }
            }
            if (i > max_frames_decode) {
                break;
            }
        }
        av_packet_unref(packet);
    }

    sws_freeContext(img_convert_context);
    avformat_close_input(&format_context);
    av_packet_free(&packet);
    avcodec_free_context(&codec_context);
    av_frame_free(&frame);
    av_frame_free(&frame_rgb);
}
