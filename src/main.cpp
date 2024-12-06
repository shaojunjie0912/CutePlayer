#include <cstdint>
#include <cstdio>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
};

int main(int argc, char* argv[]) {
    char const* input_file = nullptr;
    char const* output_file = nullptr;
    if (argc < 2) {
        printf("Usage: ./build/src/main <input_file> <output_file>\n");
        return -1;
    } else if (argc >= 2) {
        input_file = argv[1];   // 输入 ts 文件
        output_file = argv[2];  // 输出 h264 文件
    }

    FILE* fp_265 = fopen(output_file, "wb+");

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
    avcodec_parameters_to_context(codec_context, codec_params);
    int ret = avcodec_open2(codec_context, codec, nullptr);
    if (ret < 0) {
        printf("Could not open codec.\n");
        return -1;
    }
    AVPacket* packet = av_packet_alloc();
    while (av_read_frame(format_context, packet) >= 0) {
        if (packet->stream_index == video_stream_index) {
            fwrite(packet->data, 1, packet->size, fp_265);
            int ret = avcodec_send_packet(codec_context, packet);
            if (ret < 0) {
                break;
            }
        }
        av_packet_unref(packet);
    }

    fclose(fp_265);

    avformat_close_input(&format_context);
    av_packet_free(&packet);
    avcodec_free_context(&codec_context);
}
