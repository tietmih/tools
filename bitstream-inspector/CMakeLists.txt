cmake_minimum_required(VERSION 3.16)
project(bsinspector VERSION 0.3.1 LANGUAGES CXX)

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
    target_compile_options(bs_core PRIVATE /W4 /WX)
    target_compile_definitions(bs_core PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(bs_core PRIVATE -Wall -Wextra)
endif()

# Optional FFmpeg media layer (container demux + decode).
# Windows uses the bundled mingw-w64 build in third_party/; other hosts use the
# system FFmpeg development libraries when they are installed (CI and desktop
# Linux). The layer stays optional either way.
set(BS_FFMPEG_BUNDLED "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg-win64")
set(BS_FFMPEG_INCLUDE_DIR "")
set(BS_FFMPEG_LIBRARY_DIR "")
set(BS_FFMPEG_LIBRARIES "")
if(WIN32 AND EXISTS "${BS_FFMPEG_BUNDLED}/include/libavformat/avformat.h")
    set(BS_FFMPEG_INCLUDE_DIR "${BS_FFMPEG_BUNDLED}/include")
    set(BS_FFMPEG_LIBRARY_DIR "${BS_FFMPEG_BUNDLED}/lib")
elseif(NOT WIN32)
    find_path(BS_SYSTEM_FFMPEG_INCLUDE_DIR libavformat/avformat.h)
    find_library(BS_AVFORMAT_LIBRARY avformat)
    find_library(BS_AVCODEC_LIBRARY avcodec)
    find_library(BS_AVUTIL_LIBRARY avutil)
    find_library(BS_SWSCALE_LIBRARY swscale)
    if(BS_SYSTEM_FFMPEG_INCLUDE_DIR AND BS_AVFORMAT_LIBRARY AND
       BS_AVCODEC_LIBRARY AND BS_AVUTIL_LIBRARY AND BS_SWSCALE_LIBRARY)
        set(BS_FFMPEG_INCLUDE_DIR ${BS_SYSTEM_FFMPEG_INCLUDE_DIR})
        set(BS_FFMPEG_LIBRARIES ${BS_AVFORMAT_LIBRARY} ${BS_AVCODEC_LIBRARY}
            ${BS_AVUTIL_LIBRARY} ${BS_SWSCALE_LIBRARY})
    endif()
endif()

set(BS_MEDIA_TARGET "")
if(BS_FFMPEG_INCLUDE_DIR)
    add_library(bs_media STATIC src/media/media_file.cpp)
    target_link_libraries(bs_media PUBLIC bs_core)
    target_include_directories(bs_media PUBLIC ${BS_FFMPEG_INCLUDE_DIR})
    if(BS_FFMPEG_LIBRARIES)
        target_link_libraries(bs_media PUBLIC ${BS_FFMPEG_LIBRARIES})
    else()
        target_link_directories(bs_media PUBLIC ${BS_FFMPEG_LIBRARY_DIR})
        target_link_libraries(bs_media PUBLIC avformat avcodec avutil swscale)
    endif()
    target_compile_definitions(bs_media PUBLIC BS_HAVE_FFMPEG=1)
    if(MSVC)
        target_compile_options(bs_media PRIVATE /W4 /WX)
    else()
        target_compile_options(bs_media PRIVATE -Wall -Wextra
            -Wno-deprecated-declarations)
    endif()
    set(BS_MEDIA_TARGET bs_media)
endif()

# Host-side validation CLI (Linux or Windows console)
add_executable(bsinfo tools/bsinfo.cpp)
target_link_libraries(bsinfo PRIVATE bs_core)
if(MSVC)
    target_compile_options(bsinfo PRIVATE /W4 /WX)
    target_compile_definitions(bsinfo PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
if(MINGW)
    target_link_options(bsinfo PRIVATE -static-libgcc -static-libstdc++)
    target_link_options(bsinfo PRIVATE -Wl,--no-insert-timestamp)
endif()

if(BS_MEDIA_TARGET)
    target_link_libraries(bsinfo PRIVATE ${BS_MEDIA_TARGET})
    add_executable(mtest tools/mtest.cpp)
    target_link_libraries(mtest PRIVATE bs_core ${BS_MEDIA_TARGET})
    if(MSVC)
        target_compile_options(mtest PRIVATE /W4 /WX)
        target_compile_definitions(mtest PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    if(MINGW)
        target_link_options(mtest PRIVATE -static-libgcc -static-libstdc++)
        target_link_options(mtest PRIVATE -Wl,--no-insert-timestamp)
    endif()
endif()

# Host-side unit tests (portable core only).
option(BS_BUILD_TESTS "Build the unit tests" ON)
if(BS_BUILD_TESTS)
    enable_testing()
    add_executable(bs_tests tests/unit_tests.cpp)
    target_link_libraries(bs_tests PRIVATE bs_core)
    if(MSVC)
        target_compile_options(bs_tests PRIVATE /W4 /WX)
    endif()
    add_test(NAME unit COMMAND bs_tests)
endif()

if(WIN32)
    # rc.exe wants /I, windres accepts both; the include path lets the .rc find
    # app.manifest and app.ico next to itself.
    if(MSVC)
        set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} /I\"${CMAKE_CURRENT_SOURCE_DIR}/res\"")
    else()
        set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} -I${CMAKE_CURRENT_SOURCE_DIR}/res")
    endif()
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
    target_compile_definitions(bsinspector PRIVATE UNICODE _UNICODE NOMINMAX
        WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
    if(MSVC)
        target_compile_options(bsinspector PRIVATE /W4)
    endif()
    if(MINGW)
        target_link_options(bsinspector PRIVATE -municode)
        target_link_options(bsinspector PRIVATE -static-libgcc -static-libstdc++)
        target_link_options(bsinspector PRIVATE -Wl,--no-insert-timestamp)
    elseif(MSVC)
        # app.rc already embeds app.manifest; stop the linker adding a second one.
        target_link_options(bsinspector PRIVATE /MANIFEST:NO)
    endif()
endif()

if(BS_MEDIA_TARGET)
    file(GLOB BS_FFMPEG_DLLS "${BS_FFMPEG_BUNDLED}/bin/*.dll")
    # System FFmpeg on Linux has no DLLs to copy; only Windows packaging ships
    # the bundled shared libraries next to the executables.
    if(BS_FFMPEG_DLLS)
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
endif()
