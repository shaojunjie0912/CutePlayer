#include <algorithm>
#include <cstdint>
#include <cstdio>

// TODO: FILE 改为 C++ 的 fstream

extern "C" {
#include <SDL2/SDL.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
};

#define AUDIO_BUFFER_SIZE 1024

// Audio playback callback
void audio_callback(void *userdata, Uint8 *stream, int len) {
    AVCodecContext *audio_codec_context = (AVCodecContext *)userdata;
    static AVFrame *audio_frame = av_frame_alloc();
    static uint8_t *audio_buffer = nullptr;
    static int audio_buffer_len = 0;
    static int audio_buffer_index = 0;

    // If audio buffer is empty, decode a new audio packet
    if (audio_buffer_index >= audio_buffer_len) {
        AVPacket audio_packet;
        int ret = av_read_frame(audio_codec_context->opaque, &audio_packet);
        if (ret < 0) {
            // End of stream, fill with silence
            memset(stream, 0, len);
            return;
        }

        // Send the audio packet to decoder
        ret = avcodec_send_packet(audio_codec_context, &audio_packet);
        if (ret < 0) {
            printf("Error sending audio packet for decoding.\n");
            return;
        }

        // Receive decoded frame
        ret = avcodec_receive_frame(audio_codec_context, audio_frame);
        if (ret < 0) {
            printf("Error receiving audio frame.\n");
            return;
        }

        // Convert audio frame to PCM format
        audio_buffer_len = audio_frame->linesize[0];
        audio_buffer = (uint8_t *)av_malloc(audio_buffer_len);
        memcpy(audio_buffer, audio_frame->data[0], audio_buffer_len);

        audio_buffer_index = 0;
    }

    // Fill the SDL stream buffer with decoded audio
    int len_to_copy = std::min(len, audio_buffer_len - audio_buffer_index);
    memcpy(stream, audio_buffer + audio_buffer_index, len_to_copy);
    audio_buffer_index += len_to_copy;

    if (audio_buffer_index >= audio_buffer_len) {
        // Free the buffer once it's fully consumed
        av_free(audio_buffer);
        audio_buffer = nullptr;
    }
}

int main(int argc, char *argv[]) {
    const char *input_file = nullptr;
    int ret = -1;

#ifdef MP4_FILE
    input_file = MP4_FILE;
#else
    printf("Please provide an input file.\n");
    return -1;
#endif

    ret = SDL_Init(SDL_INIT_AUDIO);  // Initialize audio only
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
        printf("Could not find stream information.\n");
        return -1;
    }

    AVCodecParameters *codec_params = nullptr;
    AVCodec const *codec = nullptr;
    AVCodecContext *audio_codec_context = nullptr;
    int audio_stream_index = -1;

    for (int i = 0; i < format_context->nb_streams; ++i) {
        AVCodecParameters *local_codec_params = format_context->streams[i]->codecpar;
        AVCodec const *local_codec = avcodec_find_decoder(local_codec_params->codec_id);
        if (!local_codec) {
            printf("Unsupported codec!\n");
            return -1;
        }

        if (local_codec_params->codec_type == AVMEDIA_TYPE_AUDIO) {
            printf("[音频] 编解码器: %s; 通道数: %d, 采样率: %dHz\n", local_codec->name,
                   local_codec_params->ch_layout.nb_channels, local_codec_params->sample_rate);
            audio_stream_index = i;
            audio_codec_context = avcodec_alloc_context3(local_codec);
            ret = avcodec_parameters_to_context(audio_codec_context, local_codec_params);
            if (ret < 0) {
                printf("Could not copy audio codec context.\n");
                return -1;
            }
            ret = avcodec_open2(audio_codec_context, local_codec, nullptr);
            if (ret < 0) {
                printf("Could not open audio codec.\n");
                return -1;
            }
        }
    }

    // Set up audio playback
    SDL_AudioSpec audio_spec;
    audio_spec.freq = audio_codec_context->sample_rate;
    audio_spec.format = AUDIO_S16SYS;  // 16-bit signed integer
    audio_spec.channels = audio_codec_context->ch_layout.nb_channels;
    audio_spec.samples = AUDIO_BUFFER_SIZE;  // buffer size
    audio_spec.callback = audio_callback;
    audio_spec.userdata = audio_codec_context;

    ret = SDL_OpenAudio(&audio_spec, nullptr);
    if (ret < 0) {
        printf("SDL: Could not open audio - %s\n", SDL_GetError());
        return -1;
    }

    AVPacket *packet = av_packet_alloc();
    SDL_PauseAudio(0);  // Start audio playback

    while (av_read_frame(format_context, packet) >= 0) {
        if (packet->stream_index == audio_stream_index) {
            audio_callback(audio_codec_context, nullptr, 0);  // Handle audio decoding
        }
        av_packet_unref(packet);
    }

    SDL_Quit();
    return 0;
}
