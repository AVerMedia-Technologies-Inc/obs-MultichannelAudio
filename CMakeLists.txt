cmake_minimum_required(VERSION 3.16...3.26)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

# Set built time
# https://stackoverflow.com/a/71763529/21177210
string(TIMESTAMP MY_BUILT_TIME "%Y/%m/%d %H:%M:%S")
message("Built Time: ${MY_BUILT_TIME}")
add_compile_definitions(MY_BUILT_TIME_STR="${MY_BUILT_TIME}")

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" OFF)
option(ENABLE_QT "Use Qt functionality" OFF)

add_compile_definitions(_X64_MACHINE)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_qt(COMPONENTS Widgets Core)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt::Core Qt::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME} PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header
                                  -Wno-comma>)
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON
               AUTOUIC ON
               AUTORCC ON)
endif()

target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/plugin-main.c)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

# FFmpeg
set(ENABLE_FFMPEG_AUDIO_DECODE TRUE) # TRUE or FALSE
include(cmake/FFmpeg.cmake)

if (WIN32)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        src/Win/avt-win-audio-dshow-input.cpp
        src/Win/encode-dstr.hpp
        src/Win/AVerMediaAudioDShowInput.h
        src/Win/AVerMediaAudioDShowInput.cpp
    )
endif()

if (APPLE)
    add_compile_definitions(MACOS)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
       src/Mac/avt-mac-coreaudio-input.cpp
       src/Mac/audio-device-enum.cpp
       src/Mac/audio-device-enum.h
       src/Mac/AVerMediaCoreAudioSource.cpp
       src/Mac/AVerMediaCoreAudioSource.h
    )

    find_library(CORE_AUDIO_LIBRARY CoreAudio)
    find_library(AUDIO_TOOLBOX_LIBRARY AudioToolbox)

    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
        ${CORE_AUDIO_LIBRARY}
        ${AUDIO_TOOLBOX_LIBRARY}
    )
endif()

# avt_dshow_library
if (WIN32)
    set(ENABLE_AVT_DSHOW_LIBRARY TRUE) # TRUE or FALSE
    include(avt_dshow_core/avt_dshow_core.cmake)
    if (ENABLE_AVT_DSHOW_LIBRARY)
        add_target_additional_deps(${CMAKE_PROJECT_NAME} MORE_FILES
            "${CMAKE_CURRENT_SOURCE_DIR}/avt_dshow_core/bin/avt_dshow_core.dll"
        )
    endif()
endif()

# avt_device_opener_library
set(ENABLE_AVT_DEVICE_OPENER_LIBRARY TRUE) # TRUE or FALSE
include(avt_device_opener/avt_device_opener.cmake)
if (WIN32)
    add_target_additional_deps(${CMAKE_PROJECT_NAME} MORE_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/avt_device_opener/bin/avt_device_opener.dll"
    )
endif()
