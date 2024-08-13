message("Enable avt_device_opener_library")
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/include"
)
if (WIN32)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/lib/avt_device_opener.lib"
    )
endif()
if (APPLE)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/lib/libavt_device_opener.a"
    )
endif()

function(copy_avt_device_opener_library target)
        if (WIN32)
            if (AVT_OBS_PLUGIN_PROTOTYPE_SRC)
                set(current_project_dir "${CMAKE_CURRENT_LIST_DIR}/../..")
            else()
                set(current_project_dir "${CMAKE_CURRENT_LIST_DIR}")
            endif()
            add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy
                            "${current_project_dir}/avt_device_opener/bin/avt_device_opener.dll"
                            "${target}"
            )
            message("Copy avt_device_opener.dll to")
            message("${target}")
        endif()
endfunction()

