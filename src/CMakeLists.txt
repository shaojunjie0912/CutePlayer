add_executable(mp4_rgb24_ppm mp4_rgb24_ppm.cpp)
add_executable(use_sdl2 use_sdl2.cpp)

target_link_libraries(
  mp4_rgb24_ppm
  PUBLIC avcodec
         avformat
         avfilter
         avdevice
         swresample
         swscale
         avutil)

target_link_libraries(
  use_sdl2
  PUBLIC avcodec
         avformat
         avfilter
         avdevice
         swresample
         swscale
         avutil
         SDL2)
