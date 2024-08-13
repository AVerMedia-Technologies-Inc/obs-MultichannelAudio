/*
Plugin Name
Copyright (C) <Year> <Developer> <Email Address>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include <obs-module.h>
#include <plugin-support.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

#ifdef WIN32
extern void RegisterLogHelper();
extern void RegisterAVerMediaAudioDShowInput();
#endif // WIN32
#ifdef MACOS
extern void RegisterAVerMediaCoreAudioInput();
#endif // MACOS

extern void LoadVendorSdk();
extern void UnloadVendorSdk();

bool obs_module_load(void)
{
	obs_log(LOG_INFO, "version %s (%s)", PLUGIN_VERSION, MY_BUILT_TIME_STR);

	LoadVendorSdk();
#ifdef WIN32
	RegisterLogHelper();
	RegisterAVerMediaAudioDShowInput();
#endif // WIN32
#ifdef MACOS
    RegisterAVerMediaCoreAudioInput();
#endif // MACOS
	return true;
}

void obs_module_post_load(void)
{
	//obs_log(LOG_INFO, "obs_module_post_load");
}

void obs_module_unload(void)
{
	//obs_log(LOG_INFO, "plugin unloaded");
	UnloadVendorSdk();
}
