cmake_minimum_required(VERSION 3.16)
project(bsinspector VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(bs_core STATIC
    src/core/bitreader.cpp
    src/core/syntax.cpp
    src/core/fieldreader.cpp
    src/core/nal.cpp
    src/core/h264.cpp
    src/core/h265.cpp
    src/core/stream.cpp
)
target_include_directories(bs_core PUBLIC src)
if(MSVC)
    target_compile_options(bs_core PRIVATE /W4)
else()
    target_compile_options(bs_core PRIVATE -Wall -Wextra)
endif()

# Optional FFmpeg media layer (container demux + decode).
set(BS_FFMPEG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg-win64")
set(BS_MEDIA_TARGET "")
if(WIN32 AND EXISTS "${BS_FFMPEG_ROOT}/include/libavformat/avformat.h")
    add_library(bs_media STATIC src/media/media_file.cpp)
    target_link_libraries(bs_media PUBLIC bs_core)
    target_include_directories(bs_media PUBLIC ${BS_FFMPEG_ROOT}/include)
    target_link_directories(bs_media PUBLIC ${BS_FFMPEG_ROOT}/lib)
    target_link_libraries(bs_media PUBLIC avformat avcodec avutil swscale)
    target_compile_definitions(bs_media PUBLIC BS_HAVE_FFMPEG=1)
    if(MSVC)
        target_compile_options(bs_media PRIVATE /W4)
    else()
        target_compile_options(bs_media PRIVATE -Wall -Wextra
            -Wno-deprecated-declarations)
    endif()
    set(BS_MEDIA_TARGET bs_media)
endif()

# Host-side validation CLI (Linux or Windows console)
add_executable(bsinfo tools/bsinfo.cpp)
target_link_libraries(bsinfo PRIVATE bs_core)
if(MINGW)
    target_link_options(bsinfo PRIVATE -static-libgcc -static-libstdc++)
endif()

if(BS_MEDIA_TARGET)
    target_link_libraries(bsinfo PRIVATE ${BS_MEDIA_TARGET})
    add_executable(mtest tools/mtest.cpp)
    target_link_libraries(mtest PRIVATE bs_core ${BS_MEDIA_TARGET})
    if(MINGW)
        target_link_options(mtest PRIVATE -static-libgcc -static-libstdc++)
    endif()
endif()

if(WIN32)
    set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} -I${CMAKE_CURRENT_SOURCE_DIR}/res")
    add_executable(bsinspector WIN32
        src/ui/main.cpp
        src/ui/frame_list.cpp
        src/ui/syntax_tree.cpp
        src/ui/hex_view.cpp
        src/ui/video_view.cpp
        src/ui/chart_view.cpp
        src/ui/lang.cpp
        src/ui/ui_util.cpp
        res/app.rc
    )
    target_link_libraries(bsinspector PRIVATE bs_core
        comctl32 comdlg32 gdi32 user32 shell32)
    if(BS_MEDIA_TARGET)
        target_link_libraries(bsinspector PRIVATE ${BS_MEDIA_TARGET})
    endif()
    target_compile_definitions(bsinspector PRIVATE UNICODE _UNICODE)
    if(MINGW)
        target_link_options(bsinspector PRIVATE -municode)
        target_link_options(bsinspector PRIVATE -static-libgcc -static-libstdc++)
    endif()
endif()

if(BS_MEDIA_TARGET)
    file(GLOB BS_FFMPEG_DLLS "${BS_FFMPEG_ROOT}/bin/*.dll")
    set(BS_DLL_TARGETS bsinfo mtest)
    if(WIN32)
        list(APPEND BS_DLL_TARGETS bsinspector)
    endif()
    foreach(target ${BS_DLL_TARGETS})
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${BS_FFMPEG_DLLS} $<TARGET_FILE_DIR:${target}>
            COMMAND_EXPAND_LISTS)
    endforeach()
endif()
