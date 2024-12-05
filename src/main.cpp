#include <cstdint>
#include <cstdio>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
};

// TODO: 未增加任何返回值检测

int main(int argc, char* argv[]) {
    char const* input_file = nullptr;
    char const* output_file = nullptr;
    if (argc < 2) {
        printf("没有指定输入/输出文件！\n");
        printf("Usage: ./build/src/main input_file output_file\n");
        return -1;
    } else if (argc >= 2) {
        input_file = argv[1];
        output_file = argv[2];
    }

    FILE* fp_265 = fopen(output_file, "wb+");
    FILE* fp_yuv = fopen("", "wb+");

    // 1. 分配格式上下文空间
    AVFormatContext* format_context = avformat_alloc_context();

    // 2. 打开视频/音频流文件
    avformat_open_input(&format_context, input_file, nullptr, nullptr);

    // 3. 获取流信息
    // - format_context->iformat->name
    // - format_context->duration
    // - format_context->bit_rate
    avformat_find_stream_info(format_context, nullptr);

    // 4. 循环获取视频/音频流
    AVCodecParameters* codec_params = nullptr;  // 被编码流的各种属性
    AVCodec const* codec = nullptr;             // 编解码器信息
    int video_stream_index = -1;                // 视频流索引

    for (int i = 0; i < format_context->nb_streams; ++i) {
        // 获取编解码器参数
        AVCodecParameters* local_codec_params = format_context->streams[i]->codecpar;
        // 根据编解码器类型 ID (AV_CODEC_ID_H265, AV_CODEC_ID_HEVC...) 查找解码器
        // hevc & aac
        AVCodec const* local_codec = avcodec_find_decoder(local_codec_params->codec_id);
        // HACK: 咋 codec->name 不放在 codec_params 里面？因为里面放了 codec_id
        if (local_codec_params->codec_type == AVMEDIA_TYPE_VIDEO) {
            printf("[视频] 编解码器: %s; 分辨率: %dx%d\n", local_codec->name,
                   local_codec_params->width, local_codec_params->height);
            // 更新: 视频编解码器参数 & 编解码器
            codec_params = local_codec_params;
            codec = local_codec;
            video_stream_index = i;
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
            // size_t fwrite(const void* buffer, size_t size, size_t count, FILE* stream);
            // - buffer: 指向数组中要被写入的首个对象的指针
            // - size: 每个对象的大小(这里是 1 字节 <-> uint8_t)
            // - count: 要被写入的对象数
            // - stream: 指向输出流的指针
            fwrite(packet->data, 1, packet->size, fp_265);

            // 发送包至编解码器上下文
            int ret = avcodec_send_packet(codec_context, packet);
            if (ret < 0) {
                break;
            }
            // NOTE: 一个 AVPacket 可能会解码出多个 AVFrame
            // 确保从解码器中提取所有可能的解码帧
            // while (ret >= 0) {
            //     ret = avcodec_receive_frame(codec_context, frame);
            //     // AVERROR(EAGAIN): 当前的包数据不足以生成完整的帧，需要更多的包来解码完整帧
            //     // AVERROR_EOF: 已解码完所有帧
            //     if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF || ret < 0) {
            //         break;
            //     }
            //     // NOTE: 解码出来的 Frame 有黑边，因此需要对其进行缩放
            //     sws_scale(img_convert_context, (uint8_t const* const*)frame->data,
            //     frame->linesize,
            //               0, codec_context->height, frame_yuv->data, frame_yuv->linesize);
            //     int y_size = codec_context->width * codec_context->height;
            //     // NOTE: frame_yuv->data 是 uint8_t* [n] 指针数组
            //     fwrite(frame_yuv->data[0], 1, y_size, fp_yuv);      // Y 分量
            //     fwrite(frame_yuv->data[1], 1, y_size / 4, fp_yuv);  // U 分量
            //     fwrite(frame_yuv->data[2], 1, y_size / 4, fp_yuv);  // V 分量
            // }
        }
        av_packet_unref(packet);  // 用完必须释放 packet
    }

    fclose(fp_265);
    fclose(fp_yuv);

    sws_freeContext(img_convert_context);
    avformat_close_input(&format_context);
    av_packet_free(&packet);
    av_frame_free(&frame);
    av_frame_free(&frame_yuv);
    avcodec_free_context(&codec_context);
}
