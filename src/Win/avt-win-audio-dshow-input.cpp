#include <math.h>
#include <util/bmem.h>
#include <util/threading.h>
#include <util/platform.h>
#include <obs.h>
#include <obs-module.h>
#include <plugin-support.h>

#include "AVerMediaAudioDShowInput.h"
#include "AVerMediaVendorSdkLoader.h"
#include "encode-dstr.hpp"
#include "LogHelper.h"
#include <limits>
#include <thread>
#include <chrono>

#undef min
#undef max

/* ------------------------------------------------------------------------- */

#define AUDIO_DEVICE_ID   "audio_device_id"
#define LAST_AUDIO_DEV_ID "last_audio_device_id"
#define TEXT_DEVICE        obs_module_text("Device")

static AVerMedia::VendorSdk* g_vendorSdk = nullptr;

static void PropertiesDataDestroy(void* data)
{
	delete reinterpret_cast<AVerMedia::PropertiesData*>(data);
}

static size_t AddDevice(obs_property_t* device_list, const std::string& id)
{
	DStr name, path;
	if (!DecodeDeviceDStr(name, path, id.c_str())) {
		return std::numeric_limits<size_t>::max();
	}

	return obs_property_list_add_string(device_list, name, id.c_str());
}

static bool UpdateDeviceList(obs_property_t* list, const std::string& id)
{
	size_t size = obs_property_list_item_count(list);
	bool found = false;
	bool disabled_unknown_found = false;

	for (size_t i = 0; i < size; i++) {
		if (obs_property_list_item_string(list, i) == id) {
			found = true;
			continue;
		}
		if (obs_property_list_item_disabled(list, i))
			disabled_unknown_found = true;
	}

	if (!found && !disabled_unknown_found) {
		size_t idx = AddDevice(list, id);
		obs_property_list_item_disable(list, idx, true);
		return true;
	}

	if (found && !disabled_unknown_found)
		return false;

	for (size_t i = 0; i < size;) {
		if (obs_property_list_item_disabled(list, i)) {
			obs_property_list_item_remove(list, i);
			continue;
		}
		i += 1;
	}

	return true;
}

static bool DeviceSelectionChanged(obs_properties_t* props, 
								   obs_property_t* p,
	                               obs_data_t* settings)
{
	obs_log(LOG_DEBUG, "DeviceSelectionChanged 0");

	AVerMedia::PropertiesData* data =
		(AVerMedia::PropertiesData*)obs_properties_get_param(props);

	std::string id = obs_data_get_string(settings, AUDIO_DEVICE_ID);	
	std::string old_id = obs_data_get_string(settings, LAST_AUDIO_DEV_ID);

	obs_log(LOG_INFO, "DeviceSelectionChanged AUDIO_DEVICE_ID %s", id.c_str());
	obs_log(LOG_INFO, "DeviceSelectionChanged LAST_AUDIO_DEV_ID %s", old_id.c_str());

	bool device_list_updated = UpdateDeviceList(p, id);

	AVerMedia::DeviceInfo device;
	if (!data->GetDevice(device, id.c_str())) {
		return !device_list_updated;
	}

	/* only refresh properties if device legitimately changed */
	if (!id.size() || !old_id.size() || id != old_id) {
		obs_data_set_string(settings, LAST_AUDIO_DEV_ID, id.c_str());
	}

	obs_log(LOG_DEBUG, "DeviceSelectionChanged 1");

	return true;
}

static bool AddAudioDevice(obs_property_t* device_list,
	const AVerMedia::DeviceInfo& device)
{
	DStr name, path, device_id;

	dstr_from_wcs(name, device.name.c_str());
	dstr_from_wcs(path, device.path.c_str());

	encode_dstr(path);

	dstr_copy_dstr(device_id, name);
	encode_dstr(device_id);
	dstr_cat(device_id, ":");
	dstr_cat_dstr(device_id, path);

	obs_property_list_add_string(device_list, name, device_id);

	return true;
}

static const char *avt_audio_dshow_getname(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("AVerMedia.DolbyAudio.DisplayName");
}

static void avt_audio_dshow_destroy(void *data)
{
	obs_log(LOG_INFO, "avt_audio_dshow_destroy");
	obs_log(LOG_DEBUG, "avt_audio_dshow_destroy, CURRENTID %lu", GetCurrentThreadId());
	delete reinterpret_cast<AVerMedia::AudioDShowInput*>(data);
}

static void *avt_audio_dshow_create(obs_data_t *settings, obs_source_t *source)
{
	obs_log(LOG_INFO, "avt_audio_dshow_create");
	obs_log(LOG_DEBUG, "avt_audio_dshow_create, CURRENTID %lu", GetCurrentThreadId());
    AVerMedia::AudioDShowInput* audioSource = new AVerMedia::AudioDShowInput(g_vendorSdk, settings, source);
    return audioSource;
}

static void avt_audio_dshow_update(void *data, obs_data_t *settings)
{
	obs_log(LOG_DEBUG, "avt_audio_dshow_update, CURRENTID %lu", GetCurrentThreadId());
	reinterpret_cast<AVerMedia::AudioDShowInput*>(data)->Update(settings);
}

static void avt_audio_dshow_activate(void *data)
{
	obs_log(LOG_INFO, "avt_audio_dshow_activate");
	obs_log(LOG_DEBUG, "avt_audio_dshow_activate, CURRENTID %lu", GetCurrentThreadId());
	reinterpret_cast<AVerMedia::AudioDShowInput*>(data)->SetActive(true);
}

static void avt_audio_dshow_deactivate(void *data)
{
	obs_log(LOG_INFO, "avt_audio_dshow_deactivate");
	obs_log(LOG_DEBUG, "avt_audio_dshow_deactivate, CURRENTID %lu", GetCurrentThreadId());
	reinterpret_cast<AVerMedia::AudioDShowInput*>(data)->SetActive(false);
}

static void avt_audio_dshow_get_default(obs_data_t *settings)
{
	obs_log(LOG_DEBUG, "avt_audio_dshow_get_default");

	obs_data_set_default_bool(settings, "active", true);
	obs_data_set_default_bool(settings, "enable_ffmpeg_decode", true);
}

static obs_properties_t *avt_audio_dshow_get_properties(void *obj)
{
	obs_log(LOG_DEBUG, "avt_audio_dshow_get_properties");

	AVerMedia::AudioDShowInput* src = reinterpret_cast<AVerMedia::AudioDShowInput*>(obj);
	obs_properties_t* props = obs_properties_create();
	AVerMedia::PropertiesData* data = new AVerMedia::PropertiesData;

	data->input = src;

	obs_properties_set_param(props, data, PropertiesDataDestroy);

	obs_property_t* device_prop = obs_properties_add_list(
		props, AUDIO_DEVICE_ID, TEXT_DEVICE,
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

	obs_property_set_modified_callback(device_prop, DeviceSelectionChanged);
	
	AVerMedia::AudioDevice::GetDeviceList(data->audioDevices);
	for (const AVerMedia::DeviceInfo& device : data->audioDevices) {
		AddAudioDevice(device_prop, device);
	}

	return props;
}

static void obs_log_preFormatted(int log_level, const char* format, va_list args) {
	va_list args_copy;
	va_copy(args_copy, args);

	int length = vsnprintf(NULL, 0, format, args) + 1;
	char* formatted_str = (char*)malloc(length);
	vsnprintf(formatted_str, length, format, args);
	obs_log(log_level, "%s", formatted_str);
	free(formatted_str);

	va_end(args_copy);
}

static void initializeLogHandlers()
{
	LogHelper::setDebugHandler([](const char* format, va_list args) {
		obs_log_preFormatted(LOG_DEBUG, format, args);
	});

	LogHelper::setErrorHandler([](const char* format, va_list args) {
		obs_log_preFormatted(LOG_ERROR, format, args);
	});

	LogHelper::setWarningHandler([](const char* format, va_list args) {
		obs_log_preFormatted(LOG_WARNING, format, args);
	});

	LogHelper::setInfoHandler([](const char* format, va_list args) {
		obs_log_preFormatted(LOG_INFO, format, args);
	});
}

extern "C" {
void RegisterLogHelper()
{
	initializeLogHandlers();
}

void RegisterAVerMediaAudioDShowInput()
{
	struct obs_source_info avt_audio_dshow_input = {};
	avt_audio_dshow_input.id = "avt_audio_dshow_source",
	avt_audio_dshow_input.type = OBS_SOURCE_TYPE_INPUT,
	avt_audio_dshow_input.output_flags = OBS_SOURCE_AUDIO,
	avt_audio_dshow_input.get_name = avt_audio_dshow_getname,
	avt_audio_dshow_input.create = avt_audio_dshow_create,
	avt_audio_dshow_input.destroy = avt_audio_dshow_destroy,
	avt_audio_dshow_input.update = avt_audio_dshow_update;
	avt_audio_dshow_input.activate = avt_audio_dshow_activate;
	avt_audio_dshow_input.deactivate = avt_audio_dshow_deactivate;
	avt_audio_dshow_input.get_defaults = avt_audio_dshow_get_default;
	avt_audio_dshow_input.get_properties = avt_audio_dshow_get_properties;
	avt_audio_dshow_input.icon_type = OBS_ICON_TYPE_AUDIO_INPUT;
	obs_register_source(&avt_audio_dshow_input);
}
void LoadVendorSdk()
{
    obs_log(LOG_INFO, "LoadVendorSdk");
    if (g_vendorSdk == nullptr) {
        obs_log(LOG_INFO, "LoadVendorSdk g_vendorSdk == nullptr");
		auto mod = obs_current_module();
		auto path = obs_get_module_data_path(mod);
        obs_log(LOG_INFO, "LoadVendorSdk path: %s", path);
		g_vendorSdk = new AVerMedia::VendorSdk(path);
		g_vendorSdk->initialize();
		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	}
}
void UnloadVendorSdk()
{
	obs_log(LOG_DEBUG, "UnloadVendorSdk");
	if (g_vendorSdk != nullptr) {
		g_vendorSdk->closePort();
		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
		g_vendorSdk->uninitialize();
		delete g_vendorSdk;
		g_vendorSdk = nullptr;
	}
}

} // extern "C"
