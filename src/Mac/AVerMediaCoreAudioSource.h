#ifndef COREAUDIOSOURCE_H
#define COREAUDIOSOURCE_H

#include <obs.hpp>
#include <util/threading.h>
#include <AudioUnit/AudioUnit.h>
#include <CoreAudio/CoreAudio.h>
#include <unordered_map>
#include <string>

#include "AVerMediaDeviceOpener.h"

namespace AVerMedia {

class FfmpegAudioDecode;

struct CoreAudioSource
{
    enum coreaudio_io_type {
        IO_TYPE_INPUT,
        IO_TYPE_OUTPUT,
    };

    char *device_name = nullptr;
    char *device_uid = nullptr;
    AudioUnit unit = 0;
    AudioDeviceID device_id;
    AudioBufferList *buf_list = nullptr;
    bool au_initialized = false;
    bool active = false;
    bool input = true;
    bool no_devices = false;

    uint32_t sample_rate;
    enum audio_format format;

    pthread_t reconnect_thread = nullptr;
    os_event_t *exit_event = nullptr;
    volatile bool reconnecting;
    unsigned long retry_time;

    obs_source_t *obsSource = nullptr;
    FILE *log_file = nullptr;
    FILE *log_file2 = nullptr;

    int16_t *buffer4Ffmpeg = nullptr;
    int buffer4FfmpegSize = 0;
    FfmpegAudioDecode* decode = nullptr;
    std::string sdkLibPath;
    DeviceOpener deviceOpener;
    
    CoreAudioSource(VendorSdk* sdk, obs_data_t *settings, obs_source_t *source);
    ~CoreAudioSource();

    static std::unordered_map<std::string, std::string> getDevices();

    void Update(obs_data_t *settings);

    void coreaudio_shutdown();
    void coreaudio_uninit();
    void coreaudio_try_init();
    bool coreaudio_init();
    bool coreaudio_init_unit();
    void coreaudio_begin_reconnect();

    bool coreaudio_start();
    void coreaudio_stop();
    void coreaudio_remove_hooks();

    bool coreaudio_init_format();
    bool coreaudio_init_buffer();
    bool coreaudio_init_hooks();


    bool enable_io(coreaudio_io_type type, bool enable);
};

} // namespace AVerMedia

#endif // COREAUDIOSOURCE_H
