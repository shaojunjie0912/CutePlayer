set(MP4_FILE "${PROJECT_SOURCE_DIR}/data/video.mp4")

add_executable(simple_video_player simple_video_player.cpp)

target_compile_definitions(simple_video_player PRIVATE MP4_FILE="${MP4_FILE}")

# FFmpeg
target_include_directories(simple_video_player PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_libraries(simple_video_player PRIVATE ${FFMPEG_LIBRARIES})

# SDL2
target_include_directories(simple_video_player PRIVATE ${SDL2_INCLUDE_DIRS})
target_link_libraries(simple_video_player 
    PRIVATE 
    $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main> 
    $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>)

