#set(ENABLE_FFMPEG_AUDIO_DECODE TRUE) # TRUE or FALSE

set(current_project_dir "${CMAKE_CURRENT_LIST_DIR}/..")
set(extra_obs_prebuilt_deps "obs-deps-2024-05-08")

if (WIN32)
    set(extra_obs_prebuilt_arch "x64")
endif()
if (APPLE)
    set(extra_obs_prebuilt_arch "universal")
endif()

set(extra_obs_deps_dir "${current_project_dir}/.deps")
set(extra_obs_prebuilt_deps_dir "${extra_obs_deps_dir}/${extra_obs_prebuilt_deps}-${extra_obs_prebuilt_arch}")
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
	"${extra_obs_deps_dir}/include"
	"${extra_obs_prebuilt_deps_dir}/include"
)

# enable ffmpeg
if (ENABLE_FFMPEG_AUDIO_DECODE)
	add_compile_definitions(ENABLE_FFMPEG_DECODE)
	target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        "${current_project_dir}/src/FfmpegAudioDecode.hpp"
        "${current_project_dir}/src/FfmpegAudioDecode.cpp"
    )
	if (WIN32)
		target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
			"${extra_obs_prebuilt_deps_dir}/lib/avcodec.lib"
			"${extra_obs_prebuilt_deps_dir}/lib/avdevice.lib"
			"${extra_obs_prebuilt_deps_dir}/lib/avfilter.lib"
			"${extra_obs_prebuilt_deps_dir}/lib/avformat.lib"
			"${extra_obs_prebuilt_deps_dir}/lib/avutil.lib"
			"${extra_obs_prebuilt_deps_dir}/lib/swresample.lib"
			"${extra_obs_prebuilt_deps_dir}/lib/swscale.lib"
		)
	endif()
	if (APPLE)
		target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
			"${extra_obs_prebuilt_deps_dir}/lib/libavcodec.dylib"
			"${extra_obs_prebuilt_deps_dir}/lib/libavdevice.dylib"
			"${extra_obs_prebuilt_deps_dir}/lib/libavfilter.dylib"
			"${extra_obs_prebuilt_deps_dir}/lib/libavformat.dylib"
			"${extra_obs_prebuilt_deps_dir}/lib/libavutil.dylib"
			"${extra_obs_prebuilt_deps_dir}/lib/libswresample.dylib"
			"${extra_obs_prebuilt_deps_dir}/lib/libswscale.dylib"
		)
	endif()
endif()

function(copy_ffmpeg_library target)
    if (WIN32)
        # Define your string array
        set(ffmpeg_dll_array "avcodec-61.dll" "avdevice-61.dll" "avfilter-10.dll"
            "avformat-61.dll" "avutil-59.dll" "swresample-5.dll" "swscale-8.dll" "zlib.dll")

        # Loop through each element
        foreach(ITEM ${ffmpeg_dll_array})
            add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy
                    "${extra_obs_prebuilt_deps_dir}/bin/${ITEM}"
                    "${target}"
            )
        endforeach()
    endif()
endfunction()
