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
    avformat_find_stream_info(format_context, nullptr);

    // NOTE: 这里比特率并不等于视频编解码器比特率+音频编解码器比特率
    printf("封装器格式 %s, 时长 %ld s, 比特率 %ld bit/s\n", format_context->iformat->name,
           (format_context->duration) / 1000000, format_context->bit_rate);
}
