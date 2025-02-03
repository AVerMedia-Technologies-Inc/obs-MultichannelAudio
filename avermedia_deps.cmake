# Set built time
# https://stackoverflow.com/a/71763529/21177210
string(TIMESTAMP MY_BUILT_TIME "%Y/%m/%d %H:%M:%S")
message(STATUS "Built Time: ${MY_BUILT_TIME}")
add_compile_definitions(MY_BUILT_TIME_STR="${MY_BUILT_TIME}")

# referenced from buildspec_common.cmake function(_check_dependencies)
if(NOT buildspec)
	file(READ "${CMAKE_CURRENT_LIST_DIR}/buildspec.json" buildspec)
endif()

# cmake-format: off
string(JSON dependency_data GET ${buildspec} dependencies)
string(JSON projectName GET ${buildspec} name)
string(JSON displayName GET ${buildspec} displayName)
string(JSON pluginVersion GET ${buildspec} version)
# cmake-format: on

set(OBS_PLUGIN_PROJECT_NAME ${projectName})
set(OBS_PLUGIN_DISPLAY_NAME ${displayName})
set(OBS_PLUGIN_MAIN_VERSION ${pluginVersion})

set(_obs_dependencies_list prebuilt obs-studio) # avt-center
foreach(dependency IN LISTS _obs_dependencies_list)
	# cmake-format: off
	string(JSON data GET ${dependency_data} ${dependency})
	string(JSON version GET ${data} version)
	string(JSON label GET ${data} label)
	# cmake-format: on

	if(dependency STREQUAL avt-sw1)
		set(_sw1_sdk_version ${version})
		# cmake-format: off
		string(JSON wwUrl GET ${data} wwUrl)
		string(JSON cnUrl GET ${data} cnUrl)
		string(JSON first GET ${data} first)
		# cmake-format: on
		if (first STREQUAL "cn") # prefer to use China server
			set(_sw1_dep_url ${cnUrl})
		else()
			set(_sw1_dep_url ${wwUrl})
		endif()
	elseif(dependency STREQUAL obs-studio)
		set(_obs_version ${version})
	elseif(dependency STREQUAL avt-center)
		set(_center_version ${version})
    elseif(dependency STREQUAL prebuilt)
        set(_obs_prebuilt_version ${version})
	endif()

	#message(STATUS "Found ${label} ${version} - done")
endforeach()

# obs prebuilt version
if (WIN32)
    set(extra_obs_prebuilt_deps "obs-deps-${_obs_prebuilt_version}-x64")
    set(extra_obs_qt_prebuilt_deps "obs-deps-qt6-${_obs_prebuilt_version}-x64")
endif()
if (APPLE)
    set(extra_obs_prebuilt_deps "obs-deps-${_obs_prebuilt_version}-universal")
    set(extra_obs_qt_prebuilt_deps "obs-deps-qt6-${_obs_prebuilt_version}-universal")
endif()

# split version string
if (NOT _obs_version)
	set(_obs_major 0)
	set(_obs_minor 0)
	set(_obs_patch 0)
else()
	string(REPLACE "." ";" _obs_version_list ${_obs_version})
	list(GET _obs_version_list 0 _obs_major)
	list(GET _obs_version_list 1 _obs_minor)
	list(GET _obs_version_list 2 _obs_patch)
	#list(GET _obs_version_list 3 _obs_build) # reserved
	message(STATUS "Found dependency to OBS Studio ${_obs_version}")
endif()

# split version string
if (NOT _center_version)
	set(_center_major 0)
	set(_center_minor 0)
	set(_center_patch 0)
else()
	string(REPLACE "." ";" _center_version_list ${_center_version})
	list(GET _center_version_list 0 _center_major)
	list(GET _center_version_list 1 _center_minor)
	list(GET _center_version_list 2 _center_patch)
	#list(GET _center_version_list 3 _center_build) # reserved
	message(STATUS "Found dependency to AVerMedia Center ${_center_version}")
endif()

# Set minimal version requirement
# https://stackoverflow.com/a/29556633
set(PLUGIN_MIN_OBS_MAJOR ${_obs_major})
set(PLUGIN_MIN_OBS_MINOR ${_obs_minor})
set(PLUGIN_MIN_OBS_PATCH ${_obs_patch})
#set(PLUGIN_MIN_OBS_BUILD ${_obs_build}) # reserved
set(PLUGIN_MIN_CENTER_MAJOR ${_center_major})
set(PLUGIN_MIN_CENTER_MINOR ${_center_minor})
set(PLUGIN_MIN_CENTER_PATCH ${_center_patch})
#set(PLUGIN_MIN_CENTER_BUILD ${_center_build}) # reserved

# split plugin version string
string(REPLACE "." ";" MAIN_VERSION_list ${OBS_PLUGIN_MAIN_VERSION})
list(GET MAIN_VERSION_list 0 MAIN_VERSION_major)
list(GET MAIN_VERSION_list 1 MAIN_VERSION_minor)
list(GET MAIN_VERSION_list 2 MAIN_VERSION_patch)
# list(GET MAIN_VERSION_list 3  MAIN_VERSION_build) # reserved
message(STATUS "===== Current Plugin Version is ${OBS_PLUGIN_MAIN_VERSION}")

set(PLUGIN_MAIN_VERSION_MAJOR ${MAIN_VERSION_major})
set(PLUGIN_MAIN_VERSION_MINOR ${MAIN_VERSION_minor})
set(PLUGIN_MAIN_VERSION_PATCH ${MAIN_VERSION_patch})
#set(PLUGIN_MAIN_VERSION_BUILD ${MAIN_VERSION_build}) # reserved
