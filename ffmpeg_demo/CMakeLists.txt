macro(create_executable TARGET_NAME)
  add_executable(${TARGET_NAME} ${TARGET_NAME}.cpp)

  target_include_directories(${TARGET_NAME} PRIVATE ${FFMPEG_INCLUDE_DIRS}
                                                    ${SDL2_INCLUDE_DIRS})
  target_link_libraries(
    ${TARGET_NAME} PRIVATE ${FFMPEG_LIBRARIES} SDL2::SDL2main SDL2::SDL2
                           spdlog::spdlog)
endmacro()

create_executable(extract_audio)
create_executable(extract_video)
create_executable(remux)
create_executable(cut)
