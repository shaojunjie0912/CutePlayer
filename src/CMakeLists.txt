add_executable(main main.cpp)

set(INPUT_DIR "${PROJECT_SOURCE_DIR}/data/input")
set(OUTPUT_DIR "${PROJECT_SOURCE_DIR}/data/ouput")

file(MAKE_DIRECTORY ${OUTPUT_DIR})

# target_compile_definitions( main PUBLIC
# MP4_FILE="${INPUT_DIR}/k-on_h264_part.mp4"
# YUV_FILE="${OUTPUT_DIR}/k-on_h264_part.yuv"
# H265_FILE="${OUTPUT_DIR}/k-on_h264_part.h264")

target_link_libraries(
  main
  PUBLIC avcodec
         avformat
         avfilter
         avdevice
         swresample
         swscale
         avutil)
