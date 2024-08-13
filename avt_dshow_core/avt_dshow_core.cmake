if (WIN32)
    if (ENABLE_AVT_DSHOW_LIBRARY)
        message("Enable avt_dshow_core_library")
        target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/include"
        )
        target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/lib/avt_dshow_core.lib"
        )
    endif()
endif()

function(copy_avt_dshow_library target)
        if (WIN32)
            set(current_project_dir "${CMAKE_CURRENT_LIST_DIR}/../..")
            add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy
                            "${current_project_dir}/avt_dshow_core/bin/avt_dshow_core.dll"
                            "${target}"
            )
            message("Copy avt_dshow_core.dll to")
            message("${target}")
        endif()
endfunction()

