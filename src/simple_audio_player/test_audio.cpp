#include <cstdint>
#include <cstring>
#include <iostream>

using std::cout;
using std::endl;

extern "C" {
#include <SDL2/SDL.h>
};

struct Sound {
    uint8_t* data;
    uint32_t len;
    uint32_t pos = 0;
};

void AudioCallback(void* userdata, uint8_t* stream, int len) {
    Sound* sound = (Sound*)userdata;
    memset(stream, 0, len);
    int size = (len > sound->len - sound->pos) ? sound->len - sound->pos : len;
    memcpy(stream, sound->data + sound->pos, size);
    sound->pos += size;
}

int main(int argc, char* argv[]) {
    char const* wav_file = nullptr;
    int ret = SDL_Init(SDL_INIT_EVERYTHING);

    if (ret != 0) {
        cout << "Could not initialize SDL - " << SDL_GetError() << endl;
        return -1;
    }
#ifdef WAV_FILE
    wav_file = WAV_FILE;
#else
    cout << "没有指定音频文件" << endl;
    SQL_Quit();
    return -1;
#endif
    SDL_AudioSpec wav_spec;
    Sound sound;
    if (!SDL_LoadWAV(wav_file, &wav_spec, &sound.data, &sound.len)) {
        SDL_Log("无法加载音频文件 %s - %s", wav_file, SDL_GetError());
    }

    wav_spec.userdata = &sound;
    wav_spec.callback = AudioCallback;
    SDL_OpenAudio(&wav_spec, nullptr);
    SDL_PauseAudio(0);
    while (true) {
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
    SDL_Quit();
    return 0;
}