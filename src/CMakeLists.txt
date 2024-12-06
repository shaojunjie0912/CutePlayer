add_executable(main main.cpp)

set(INPUT_DIR "${PROJECT_SOURCE_DIR}/data/input")
set(OUTPUT_DIR "${PROJECT_SOURCE_DIR}/data/ouput")

file(MAKE_DIRECTORY ${OUTPUT_DIR})

target_link_libraries(
  main
  PUBLIC avcodec
         avformat
         avfilter
         avdevice
         swresample
         swscale
         avutil)
