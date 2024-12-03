#include <cstddef>
#include <cstdint>
#include <cstdio>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
};

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("未指定文件路径！\n");
    }
    char const* file_path = argv[1];

    // 1. 分配格式上下文空间
    AVFormatContext* format_context = avformat_alloc_context();

    // 2. 打开视频/音频流文件
    avformat_open_input(&format_context, file_path, nullptr, nullptr);

    // 3. 获取流信息
    // - format_context->iformat->name
    // - format_context->duration
    // - format_context->bit_rate
    avformat_find_stream_info(format_context, nullptr);

    // 4. 循环获取视频/音频流
    AVCodecParameters* codec_params = nullptr;  // 被编码流的各种属性
    AVCodec const* codec = nullptr;             // 编解码器信息
    int video_stream_index = -1;                // 视频流索引

    for (int i{0}; i < format_context->nb_streams; ++i) {
        // 获取编解码器参数
        AVCodecParameters* local_codec_params = format_context->streams[i]->codecpar;
        // 根据编解码器类型 ID (AV_CODEC_ID_H264, AV_CODEC_ID_HEVC...) 查找解码器
        // hevc & aac
        AVCodec const* local_codec = avcodec_find_decoder(local_codec_params->codec_id);
        // HACK: 咋 codec->name 不放在 codec_params 里面？因为里面放了 codec_id
        if (local_codec_params->codec_type == AVMEDIA_TYPE_VIDEO) {
            printf("[视频] 编解码器: %s; 分辨率: %dx%d\n", local_codec->name,
                   local_codec_params->width, local_codec_params->height);
            // 更新: 视频编解码器参数 & 编解码器
            codec_params = local_codec_params;
            codec = local_codec;
        } else if (local_codec_params->codec_type == AVMEDIA_TYPE_AUDIO) {
            printf("[音频] 编解码器: %s; 通道数: %d, 采样率: %dHz\n", local_codec->name,
                   local_codec_params->ch_layout.nb_channels, local_codec_params->sample_rate);
        }
    }
    // 5. 分配编解码器上下文内存
    AVCodecContext* codec_context = avcodec_alloc_context3(codec);

    // 6. 拷贝编解码器参数至 AVCodecContext
    avcodec_parameters_to_context(codec_context, codec_params);

    // 7. 打开解码器
    avcodec_open2(codec_context, codec, nullptr);

    // 8. 为 Frame, FrameYUV, Packet 分配内存
    AVFrame* frame = av_frame_alloc();
    AVFrame* frame_yuv = av_frame_alloc();
    AVPacket* packet = av_packet_alloc();

    // 9. 分配存储 YUV 图像数据的内存
    // 根据给定的像素格式、宽度、高度和对齐要求，计算所需的内存缓冲区大小}
    uint8_t* out_buffer = (uint8_t*)av_malloc(av_image_get_buffer_size(
        AV_PIX_FMT_YUV420P, codec_context->width, codec_context->height, 32));

    // 10. 将这块内存划分为 YUV420 格式，并将其与 pFrameYUV 的 data 和 linesize 字段关联起来，使得
    // pFrameYUV 可以正确访问和操作图像的 Y、U、V 数据。
    av_image_fill_arrays(frame_yuv->data, frame_yuv->linesize, out_buffer, AV_PIX_FMT_YUV420P,
                         codec_context->width, codec_context->height, 32);
    // 11. 创建图像转换上下文
    // 将解码前原始视频帧转换为解码后 YUV420P 格式，并应用缩放算法(这里宽高不变)
    // pCodecContext->pix_fmt 转为 YUV420P
    SwsContext* img_convert_context = sws_getContext(
        codec_context->width, codec_context->height, codec_context->pix_fmt, codec_context->width,
        codec_context->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, nullptr, nullptr, nullptr);

    // 12. 循环读取并处理视频帧
    // 使用 av_read_frame 能够获取到完整的一帧
    // 而老版本 av_read_packet 读出的是包，可能是半帧或多帧
    while (av_read_frame(format_context, packet) >= 0) {
        // NOTE: AVPacket 不一定就是视频帧
        if (packet->stream_index == video_stream_index) {
            fwrite()
        }
    }
}
