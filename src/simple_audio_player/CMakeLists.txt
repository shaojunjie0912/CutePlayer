set(AAC_FILE "${PROJECT_SOURCE_DIR}/data/audio.aac")

add_executable(simple_audio_player simple_audio_player.cpp)

target_compile_definitions(simple_audio_player PRIVATE AAC_FILE="${AAC_FILE}")