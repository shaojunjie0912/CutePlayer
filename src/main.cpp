#include <exception>
#include <iostream>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
};

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "未指定文件路径" << std::endl;
    }
    char const* file_path = argv[1];

    // 1. 分配上下文空间
    AVFormatContext* format_context = avformat_alloc_context();

    // 2. 打开视频/音频流文件
    avformat_open_input(&format_context, file_path, nullptr, nullptr);

    // 3. 获取流信息
    // - format_context->iformat->name
    // - format_context->duration
    // - format_context->bit_rate
    avformat_find_stream_info(format_context, nullptr);

    // 4. 循环获取视频/音频流
    // AVCodecParameters* codec_params = nullptr;  // 被编码流的各种属性
    // AVCodec* codec = nullptr;                   // 编解码器信息

    for (int i{0}; i < format_context->nb_streams; ++i) {
        // 获取编解码器参数
        AVCodecParameters* codec_params = format_context->streams[i]->codecpar;
        // 根据编解码器类型 ID (AV_CODEC_ID_H264, AV_CODEC_ID_HEVC...) 查找解码器
        AVCodec const* codec = avcodec_find_decoder(codec_params->codec_id);
    }
}
