#include <cstdint>
#include <string>
#include <vector>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/log.h>
}

int main(int argc, char* argv[]) {
    if (argc < 5) {
        av_log(nullptr, AV_LOG_ERROR, "Need specify input & output file & start & end time!");
        return -1;
    }
    char const* input_file = argv[1];
    char const* output_file = argv[2];
    double start_time = std::stof(argv[3]);
    double end_time = std::stof(argv[4]);

    // 打开输入文件并分配上下文空间<读取>
    AVFormatContext* input_format_context = nullptr;
    int ret = avformat_open_input(&input_format_context, input_file, nullptr, nullptr);
    if (ret < 0) {
        av_log(nullptr, AV_LOG_ERROR, "Can't open file!");
        return -1;
    }

    // 打开输出文件并分配上下文空间<写入>
    AVFormatContext* output_format_context;  // 根据输出文件后缀分配格式上下文
    avformat_alloc_output_context2(&output_format_context, nullptr, nullptr, output_file);
    if (!output_format_context) {
        av_log(nullptr, AV_LOG_ERROR, "Can't alloc output context!");
        if (input_format_context) {
            avformat_close_input(&input_format_context);
        }
        return -1;
    }

    int input_nb_streams = input_format_context->nb_streams;
    std::vector<int> stream_map(input_nb_streams, 0);
    int stream_id = 0;
    for (int i = 0; i < input_nb_streams; ++i) {
        AVCodecParameters* input_codec_params = input_format_context->streams[i]->codecpar;
        // ❌不是视频/音频/字幕! 跳过! 置 -1
        if (input_codec_params->codec_type != AVMEDIA_TYPE_VIDEO &&
            input_codec_params->codec_type != AVMEDIA_TYPE_AUDIO &&
            input_codec_params->codec_type != AVMEDIA_TYPE_SUBTITLE) {
            stream_map[i] = -1;
            continue;
        }
        // 为输出文件创建新的流
        // nullptr -> ffmpeg 根据容器格式自动选择合适的编码器
        AVStream* output_stream = avformat_new_stream(output_format_context, nullptr);
        // 拷贝编码器参数
        avcodec_parameters_copy(output_stream->codecpar, input_codec_params);
        output_stream->codecpar->codec_tag = 0;
        stream_map[i] = stream_id++;
    }

    // HACK: 这里使用 ffmpeg 的文件操作, 因此需要绑定 <输出文件> & <pb:io context>
    ret = avio_open2(&output_format_context->pb, output_file, AVIO_FLAG_WRITE, nullptr,
                     nullptr);  // 将输出多媒体文件上下文与输出多媒体文件<io>绑定
    if (ret < 0) {
        av_log(output_format_context, AV_LOG_ERROR, "Bind IO error!");
        if (input_format_context) {
            avformat_close_input(&input_format_context);
        }
        if (output_format_context) {
            avformat_close_input(&output_format_context);
        }
        return -1;
    }

    // 将 <"流头"> 写入 <输出多媒体文件>
    ret = avformat_write_header(output_format_context, nullptr);
    if (ret < 0) {
        av_log(output_format_context, AV_LOG_ERROR, "Write header error!");
        if (input_format_context) {
            avformat_close_input(&input_format_context);
        }
        if (output_format_context) {
            avformat_close_input(&output_format_context);
        }
        return -1;
    }

    // 💗 跳转至 start_time 时间戳
    // TODO: I B P 帧知识补充
    // HACK: timestamp = start * AV_TIME_BASE
    // BACKWARD 向前回退找到 I 帧
    ret = av_seek_frame(input_format_context, -1, start_time * AV_TIME_BASE, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        av_log(output_format_context, AV_LOG_ERROR, "Seek to the keyframe error!");
        if (input_format_context) {
            avformat_close_input(&input_format_context);
        }
        if (output_format_context) {
            avformat_close_input(&output_format_context);
        }
        return -1;
    }

    std::vector<int64_t> pts_start_time(input_nb_streams, -1);  // 起始 pts
    std::vector<int64_t> dts_start_time(input_nb_streams, -1);  // 起始 dts

    // 从输入多媒体文件中读取音频数据到输出多媒体文件上下文
    AVPacket* output_packet = av_packet_alloc();
    // 将帧数据写入包中
    while (av_read_frame(input_format_context, output_packet) >= 0) {
        int input_stream_idx = output_packet->stream_index;    // 输入流索引
        int output_stream_idx = stream_map[input_stream_idx];  // 输出流索引
        // ❌ 不是音频/视频/字幕流的包 pass!!!
        if (output_stream_idx < 0) {
            av_packet_unref(output_packet);
            continue;
        }

        AVStream* input_stream = input_format_context->streams[input_stream_idx];
        AVStream* output_stream = output_format_context->streams[output_stream_idx];

        // 超出结束时间退出
        // HACK: av_q2d 转换 double
        if (av_q2d(input_stream->time_base) * output_packet->pts > end_time) {
            av_packet_unref(output_packet);
            break;
        }

        // ️️⭕️ 重设输出包<流索引>
        output_packet->stream_index = output_stream_idx;

        // 保存第一个包的起始时间戳, 后面每个包都相对第一个包
        if (pts_start_time[input_stream_idx] == -1) {
            pts_start_time[input_stream_idx] = output_packet->pts;
        }
        if (dts_start_time[input_stream_idx] == -1) {
            dts_start_time[input_stream_idx] = output_packet->dts;
        }

        // ⭕️ 重设 pts & dts
        // HACK: 相对第一个包的时间戳
        output_packet->pts -= pts_start_time[input_stream_idx];  // 重设输出包<pts> (减去起始pts)
        output_packet->dts -= dts_start_time[input_stream_idx];  // 重设输出包<dts> (减去起始dts)

        // ⭕️ 重设时基
        av_packet_rescale_ts(output_packet, input_stream->time_base, output_stream->time_base);

        output_packet->pos = -1;
        // 将包数据交错写入输出格式上下文
        av_interleaved_write_frame(output_format_context, output_packet);
        // 释放包资源
        av_packet_unref(output_packet);
    }

    // 将 <"流尾"> 写入 <输出多媒体文件>
    av_write_trailer(output_format_context);

    // 释放资源
    if (input_format_context) {
        avformat_close_input(&input_format_context);
    }
    if (output_format_context) {
        avformat_close_input(&output_format_context);
    }

    return 0;
}
