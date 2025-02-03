set(extra_obs_deps_dir "${CMAKE_CURRENT_LIST_DIR}/.deps")
message(STATUS "extra_obs_deps_dir: ${extra_obs_deps_dir}")

# referenced from buildspec_common.cmake function(_check_dependencies)
if(NOT buildspec)
	file(READ "${CMAKE_CURRENT_LIST_DIR}/buildspec.json" buildspec)
endif()

# cmake-format: off
string(JSON dependency_data GET ${buildspec} dependencies)
# cmake-format: on

set(_obs_dependencies_list prebuilt qt6 obs-studio)
foreach(dependency IN LISTS _obs_dependencies_list)
	# cmake-format: off
	string(JSON data GET ${dependency_data} ${dependency})
	string(JSON version GET ${data} version)
	string(JSON label GET ${data} label)
	# cmake-format: on

	if(dependency STREQUAL prebuilt)
		set(_obs_prebuilt_version ${version})
	elseif(dependency STREQUAL qt6)
		set(_obs_qt6_version ${version})
	elseif(dependency STREQUAL obs-studio)
		set(_obs_version ${version})
	endif()

	#message(STATUS "Found ${label} ${version} - done")
endforeach()

if (WIN32)
    set(extra_obs_qt_prebuilt_deps "obs-deps-qt6-${_obs_prebuilt_version}-x64")
endif()
if (APPLE)
    set(extra_obs_qt_prebuilt_deps "obs-deps-qt6-${_obs_prebuilt_version}-universal")
endif()

set(extra_obs_qt_prebuilt_deps_cmake "${extra_obs_deps_dir}/${extra_obs_qt_prebuilt_deps}/lib/cmake")

set(Qt6_DIR              "${extra_obs_qt_prebuilt_deps_cmake}/Qt6")
set(Qt6Core_DIR          "${extra_obs_qt_prebuilt_deps_cmake}/Qt6Core")
set(Qt6Gui_DIR           "${extra_obs_qt_prebuilt_deps_cmake}/Qt6Gui")
set(Qt6Widgets_DIR       "${extra_obs_qt_prebuilt_deps_cmake}/Qt6Widgets")
set(Qt6OpenGL_DIR        "${extra_obs_qt_prebuilt_deps_cmake}/Qt6OpenGL")
set(Qt6OpenGLWidgets_DIR "${extra_obs_qt_prebuilt_deps_cmake}/Qt6OpenGLWidgets")

function(add_obs_qt_component target)    
    set(components ${ARGN})

    message(STATUS "")
    message(STATUS "add_obs_qt_component target: ${target}")
    foreach(component IN LISTS components)
        message(STATUS "add_obs_qt_component component: ${component}")
        message(STATUS "Qt6::${component}")
    endforeach()
    message(STATUS "")

    foreach(component IN LISTS components)
        find_package(Qt6 REQUIRED COMPONENTS ${component})
        target_link_libraries(${target} PRIVATE "Qt6::${component}")
    endforeach()
endfunction()
