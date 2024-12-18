set(AAC_FILE "${PROJECT_SOURCE_DIR}/data/audio.aac")
set(WAV_FILE "${PROJECT_SOURCE_DIR}/data/audio.wav")

# add_executable(simple_audio_player simple_audio_player.cpp)

# target_compile_definitions(simple_audio_player PRIVATE AAC_FILE="${AAC_FILE}")

add_executable(test_audio test_audio.cpp)

target_compile_definitions(test_audio PRIVATE WAV_FILE="${WAV_FILE}")

# FFmpeg
target_include_directories(test_audio PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_libraries(test_audio PRIVATE ${FFMPEG_LIBRARIES})

# SDL2
target_include_directories(test_audio PRIVATE ${SDL2_INCLUDE_DIRS})
target_link_libraries(test_audio 
    PRIVATE 
    $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main> 
    $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>)