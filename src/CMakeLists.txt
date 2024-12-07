add_executable(mp4_rgb24_ppm mp4_rgb24_ppm.cpp)

target_link_libraries(
  mp4_rgb24_ppm
  PUBLIC avcodec
         avformat
         avfilter
         avdevice
         swresample
         swscale
         avutil)
