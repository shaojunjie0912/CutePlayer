add_executable(main main.cpp)

target_link_libraries(
  main
  PUBLIC avcodec
         avformat
         avfilter
         avdevice
         swresample
         swscale
         avutil)
