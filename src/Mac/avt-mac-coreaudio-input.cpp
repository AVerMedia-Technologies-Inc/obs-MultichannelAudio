#include <obs-module.h>
#include <plugin-support.h>

#include "AVerMediaCoreAudioSource.h"
#include "AVerMediaVendorSdkLoader.h"
#include <thread>
#include <chrono>

#define TEXT_DEVICE        obs_module_text("Device")

static AVerMedia::VendorSdk* g_vendorSdk = nullptr;

static const char *avt_coreaudio_getname(void *unused)
{
    UNUSED_PARAMETER(unused);
    return obs_module_text("AVerMedia.DolbyAudio.DisplayName");
}

static void avt_coreaudio_destroy(void *data)
{
    obs_log(LOG_INFO, "avt_coreaudio_destroy");
    delete reinterpret_cast<AVerMedia::CoreAudioSource*>(data);
}

static void *avt_coreaudio_create(obs_data_t *settings, obs_source_t *source)
{
    obs_log(LOG_INFO, "avt_coreaudio_create");
    return new AVerMedia::CoreAudioSource(g_vendorSdk, settings, source);;
}

static void avt_coreaudio_update(void *data, obs_data_t *settings)
{
    obs_log(LOG_INFO, "avt_coreaudio_update");
    reinterpret_cast<AVerMedia::CoreAudioSource*>(data)->Update(settings);
}

static void avt_coreaudio_get_default(obs_data_t *settings)
{
    obs_log(LOG_INFO, "avt_coreaudio_get_default");
    UNUSED_PARAMETER(settings);
}

static obs_properties_t *avt_coreaudio_get_properties(void *unused)
{
    obs_log(LOG_INFO, "avt_coreaudio_get_properties");
    UNUSED_PARAMETER(unused);
	obs_properties_t *props = 
            obs_properties_create();
	
	obs_property_t *property = 
            obs_properties_add_list(props, "device_id", TEXT_DEVICE,
                                    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

	auto devices = AVerMedia::CoreAudioSource::getDevices();
    for (const auto& [deviceId, deviceName] : devices) {
        obs_log(LOG_INFO, "device_id: %s", deviceId.c_str());
        obs_log(LOG_INFO, "device_name: %s", deviceName.c_str());

        obs_property_list_add_string(property, deviceName.c_str(), deviceId.c_str());
    }
	return props;
}

extern "C" {
void RegisterAVerMediaCoreAudioInput()
{
    struct obs_source_info avt_coreaudio_input = {};
    avt_coreaudio_input.id = "avt_coreaudio_source",
    avt_coreaudio_input.type = OBS_SOURCE_TYPE_INPUT,
    avt_coreaudio_input.output_flags = OBS_SOURCE_AUDIO,
    avt_coreaudio_input.get_name = avt_coreaudio_getname,
    avt_coreaudio_input.create = avt_coreaudio_create,
    avt_coreaudio_input.destroy = avt_coreaudio_destroy,
    avt_coreaudio_input.update = avt_coreaudio_update;
    avt_coreaudio_input.get_defaults = avt_coreaudio_get_default;
    avt_coreaudio_input.get_properties = avt_coreaudio_get_properties;
    avt_coreaudio_input.icon_type = OBS_ICON_TYPE_AUDIO_INPUT;
    obs_register_source(&avt_coreaudio_input);
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
    obs_log(LOG_INFO, "UnloadVendorSdk");
	if (g_vendorSdk != nullptr) {
		g_vendorSdk->closePort();
		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
		g_vendorSdk->uninitialize();
		delete g_vendorSdk;
		g_vendorSdk = nullptr;
	}
}
} // extern "C"

