set(MP4_FILE "${PROJECT_SOURCE_DIR}/data/k-on!.mp4")

add_executable(simple_video_player simple_video_player.cpp)

target_compile_definitions(simple_video_player PRIVATE MP4_FILE="${MP4_FILE}")

