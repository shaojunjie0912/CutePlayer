// #include <cstdint>
// #include <cstring>

extern "C" {
#include <SDL2/SDL.h>
};

// struct Sound {
//     uint8_t* data;
//     uint32_t len;
//     uint32_t pos = 0;
// };

// void AudioCallback(void* userdata, uint8_t* stream, int len) {
//     Sound* sound = (Sound*)userdata;
//     memset(stream, 0, len);
//     int size = (len > sound->len - sound->pos) ? sound->len - sound->pos : len;
//     memcpy(stream, sound->data + sound->pos, size);
//     sound->pos += size;
// }

int main() {
    char const* audio_file = nullptr;
    int ret = SDL_Init(SDL_INIT_EVERYTHING);

    SDL_Window* window =
        SDL_CreateWindow("SDL Video Player", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 300,
                         300, SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI);
    //     if (ret != 0) {
    //         cout << "Could not initialize SDL - " << SDL_GetError() << endl;
    //         return -1;
    //     }
    // #ifdef WAV_FILE
    //     audio_file = WAV_FILE;
    // #else
    //     cout << "没有指定音频文件" << endl;
    // #endif
    //     SDL_AudioSpec wav_spec;
    //     Sound sound;
    //     if (!SDL_LoadWAV(audio_file, &wav_spec, &sound.data, &sound.len)) {
    //         SDL_Log("无法加载音频文件 %s - %s", audio_file, SDL_GetError());
    //     }

    //     wav_spec.userdata = &sound;
    //     wav_spec.callback = AudioCallback;
    // SDL_OpenAudio(&wav_spec, nullptr);
    // SDL_PauseAudio(0);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}