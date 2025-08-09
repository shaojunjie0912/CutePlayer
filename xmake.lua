set_project("CutePlayer")
set_xmakever("2.9.9")

set_languages("c++20")

add_rules("mode.debug", "mode.release", "mode.releasedbg")
add_rules("plugin.compile_commands.autoupdate")

set_warnings("allextra")

add_requires("ffmpeg")
add_requires("libsdl2")

target("cuteplayer", function () 
    set_kind("binary")
    add_files("src/*.cpp")
    add_includedirs("include")
    add_packages("libsdl2", "ffmpeg")
end)

