#include "AVerMediaCoreAudioSource.h"

#include "audio-device-enum.h"
#include <plugin-support.h>
#include <mach/mach_time.h>
#include <util/dstr.h>
#include <util/apple/cfstring-utils.h>
#include <media-io/audio-io.h>

#include <inttypes.h>
#include <codecvt>
#include <locale>

#ifdef ENABLE_FFMPEG_DECODE
#include "FfmpegAudioDecode.hpp"
#endif // ENABLE_FFMPEG_DECODE

#define MAX_DEVICE_INPUT_CHANNELS 64

#define PROPERTY_FORMATS kAudioStreamPropertyAvailablePhysicalFormats

#define SCOPE_OUTPUT kAudioUnitScope_Output
#define SCOPE_INPUT kAudioUnitScope_Input
#define SCOPE_GLOBAL kAudioUnitScope_Global

#define BUS_OUTPUT 0
#define BUS_INPUT 1

#define set_property AudioUnitSetProperty
#define get_property AudioUnitGetProperty

// 將 Float32 數據轉換為 16 位有符號整數，並四捨五入
static inline int16_t float32_to_int16_t(Float32 f) {
    int32_t result = (int32_t)(lroundf(f * 32768.0f));
    if (result > 32767) {
        obs_log(LOG_INFO, "float32_to_int16_t (f > 32767)");
        result = 32767;
    }
    else if (result < -32768) {
        obs_log(LOG_INFO, "float32_to_int16_t (f < -32768)");
        result = -32768;
    }
    return result;
}

static inline int16_t sint32_to_int16_t(SInt32 i) {
    return (int16_t)(i >> 16);
}

static inline std::wstring convertCharToWString(const char* cstr) {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
    // 創建用於轉換的codecvt facet
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    // 執行轉換
    std::wstring wstr = converter.from_bytes(cstr);

    return wstr;

#if defined(__clang__)
#pragma clang diagnostic pop
#endif
}

static inline void ca_warn(AVerMedia::CoreAudioSource *ca, const char *func,
                           const char *format, ...)
{
    va_list args;
    dstr str = {0};

    va_start(args, format);

    dstr_printf(&str, "[%s]:[device '%s'] ", func, ca->device_name);
    dstr_vcatf(&str, format, args);
    obs_log(LOG_WARNING, "%s", str.array);
    dstr_free(&str);

    va_end(args);
}

static inline bool ca_success(OSStatus stat, AVerMedia::CoreAudioSource *ca,
                              const char *func, const char *action)
{
    if (stat != noErr) {
        obs_log(LOG_WARNING, "[%s]:[device '%s'] %s failed: %d", func,
             ca->device_name, action, (int)stat);
        return false;
    }

    return true;
}

static inline enum audio_format convert_ca_format(UInt32 format_flags,
                                                  UInt32 bits)
{
    bool planar = (format_flags & kAudioFormatFlagIsNonInterleaved) != 0;

    if (format_flags & kAudioFormatFlagIsFloat)
        return planar ? AUDIO_FORMAT_FLOAT_PLANAR : AUDIO_FORMAT_FLOAT;

    if (!(format_flags & kAudioFormatFlagIsSignedInteger) && bits == 8)
        return planar ? AUDIO_FORMAT_U8BIT_PLANAR : AUDIO_FORMAT_U8BIT;

    /* not float?  not signed int?  no clue, fail */
    if ((format_flags & kAudioFormatFlagIsSignedInteger) == 0)
        return AUDIO_FORMAT_UNKNOWN;

    if (bits == 16)
        return planar ? AUDIO_FORMAT_16BIT_PLANAR : AUDIO_FORMAT_16BIT;
    else if (bits == 32)
        return planar ? AUDIO_FORMAT_32BIT_PLANAR : AUDIO_FORMAT_32BIT;

    return AUDIO_FORMAT_UNKNOWN;
}


static char *sanitize_device_name(char *name)
{
    const size_t max_len = 64;
    size_t len = strlen(name);
    char buf[64];
    size_t out_idx = 0;

    for (size_t i = len > max_len ? len - max_len : 0; i < len; i++) {
        char c = name[i];
        if (isalnum(c)) {
            buf[out_idx++] = name[i];
        }
        if (c == '-' || c == ' ' || c == '_' || c == ':') {
            buf[out_idx++] = '_';
        }
    }
    return bstrdup_n(buf, out_idx);
}

static void buf_list_free(AudioBufferList *buf_list)
{
    if (buf_list) {
        for (UInt32 i = 0; i < buf_list->mNumberBuffers; i++)
            bfree(buf_list->mBuffers[i].mData);

        bfree(buf_list);
    }
}

static void *reconnect_thread(void *param)
{
    AVerMedia::CoreAudioSource *ca = reinterpret_cast<AVerMedia::CoreAudioSource*>(param);

    ca->reconnecting = true;

    while (os_event_timedwait(ca->exit_event, ca->retry_time) ==
           ETIMEDOUT) {
        if (ca->coreaudio_init())
            break;
    }

    obs_log(LOG_DEBUG, "coreaudio: exit the reconnect thread");
    ca->reconnecting = false;
    return NULL;
}

static bool find_device_id_by_uid(AVerMedia::CoreAudioSource *ca)
{
    if (!ca->device_uid) {
        obs_log(LOG_INFO, "find_device_id_by_uid no device_uid");
        ca->device_uid = bstrdup("");
    }
    obs_log(LOG_INFO, "find_device_id_by_uid device_uid %s\n", ca->device_uid);

    CFStringRef cf_uid = CFStringCreateWithCString(NULL, ca->device_uid,
                                       kCFStringEncodingUTF8);

    bool success = coreaudio_get_device_id(cf_uid, &ca->device_id);

    ca->no_devices = !success;

    if (cf_uid)
        CFRelease(cf_uid);

    return success;
}

static bool coreaudio_get_device_name(AVerMedia::CoreAudioSource *ca)
{
    CFStringRef cf_name = NULL;
    UInt32 size = sizeof(CFStringRef);
    char *name = NULL;

    const AudioObjectPropertyAddress addr = {
                                             kAudioDevicePropertyDeviceNameCFString,
                                             kAudioObjectPropertyScopeInput,
                                             kAudioObjectPropertyElementMain};

    OSStatus stat = AudioObjectGetPropertyData(ca->device_id, &addr, 0,
                                               NULL, &size, &cf_name);
    if (stat != noErr) {
        obs_log(LOG_WARNING,
             "[coreaudio_get_device_name] failed to "
             "get name: %d",
             (int)stat);
        return false;
    }

    name = cfstr_copy_cstr(cf_name, kCFStringEncodingUTF8);
    if (!name) {
        obs_log(LOG_WARNING, "[coreaudio_get_device_name] failed to "
                          "convert name to cstr for some reason");
        return false;
    }

    bfree(ca->device_name);
    ca->device_name = name;

    if (cf_name)
        CFRelease(cf_name);

    return true;
}

static OSStatus
notification_callback(AudioObjectID id, UInt32 num_addresses,
                      const AudioObjectPropertyAddress addresses[], void *data)
{
    UNUSED_PARAMETER(addresses);

    AVerMedia::CoreAudioSource *ca = reinterpret_cast<AVerMedia::CoreAudioSource*>(data);

    ca->coreaudio_stop();
    ca->coreaudio_uninit();
    ca->retry_time = 2000;

    obs_log(LOG_INFO,
         "coreaudio: device '%s' disconnected or changed.  "
         "attempting to reconnect",
         ca->device_name);

    ca->coreaudio_begin_reconnect();

    UNUSED_PARAMETER(id);
    UNUSED_PARAMETER(num_addresses);

    return noErr;
}

static OSStatus add_listener(AVerMedia::CoreAudioSource *ca, UInt32 property)
{
    AudioObjectPropertyAddress addr = {property,
                                       kAudioObjectPropertyScopeGlobal,
                                       kAudioObjectPropertyElementMain};

    return AudioObjectAddPropertyListener(ca->device_id, &addr,
                                          notification_callback, ca);
}

static OSStatus input_callback(void *data,
                               AudioUnitRenderActionFlags *action_flags,
                               const AudioTimeStamp *ts_data, UInt32 bus_num,
                               UInt32 frames, AudioBufferList *ignored_buffers)
{
    AVerMedia::CoreAudioSource *ca = reinterpret_cast<AVerMedia::CoreAudioSource*>(data);

    OSStatus stat = AudioUnitRender(ca->unit, action_flags, ts_data, bus_num, frames,
                           ca->buf_list);
    if (!ca_success(stat, ca, "AVerMedia::input_callback", "audio retrieval")) {
        return noErr;
    }

    // obs_log(LOG_INFO, "input_callback frames %d", frames);
    // obs_log(LOG_INFO, "ca->format %d", ca->format);


    if (ca->format == AUDIO_FORMAT_32BIT_PLANAR && ca->buf_list->mNumberBuffers >= 2) {
        // 假設每個音頻樣本是一個 Float32 值 //SInt32
#if 0
        {
            SInt32 *buffer = (SInt32*)malloc(ca->buf_list->mBuffers[0].mDataByteSize * 2);
            SInt32 *left = (SInt32 *)ca->buf_list->mBuffers[2].mData;
            SInt32 *right = (SInt32 *)ca->buf_list->mBuffers[3].mData;

            for (UInt32 i = 0; i < ca->buf_list->mBuffers[0].mDataByteSize / sizeof(SInt32); i++) {
                buffer[i * 2] = left[i];
                buffer[i * 2 + 1] = right[i];
            }

            if (ca->log_file2 != nullptr) {
                fwrite(buffer, sizeof(SInt32), ca->buf_list->mBuffers[0].mDataByteSize * 2 / sizeof(SInt32), ca->log_file2);
            }
            free(buffer);
        }
#endif
        SInt32 *left = nullptr;
        SInt32 *right = nullptr;

        if (ca->buf_list->mNumberBuffers == 4) {
            // obs_log(LOG_INFO, "ca->buf_list->mNumberBuffers == 4");
            left = (SInt32 *)ca->buf_list->mBuffers[2].mData;
            right = (SInt32 *)ca->buf_list->mBuffers[3].mData;
        }
        else {
            left = (SInt32 *)ca->buf_list->mBuffers[0].mData;
            right = (SInt32 *)ca->buf_list->mBuffers[1].mData;
        }

        for (UInt32 i = 0; i < ca->buf_list->mBuffers[0].mDataByteSize / sizeof(SInt32); i++) {
            ca->buffer4Ffmpeg[i * 2] = left[i] >> 16;
            ca->buffer4Ffmpeg[i * 2 + 1] = right[i] >> 16;
        }
        if (ca->log_file != nullptr) {
            obs_log(LOG_INFO, "fwrite %lu", ca->buf_list->mBuffers[0].mDataByteSize / sizeof(int16_t));
            fwrite(ca->buffer4Ffmpeg, sizeof(int16_t), ca->buf_list->mBuffers[0].mDataByteSize / sizeof(int16_t), ca->log_file);
        }

#ifdef ENABLE_FFMPEG_DECODE
//        obs_log(LOG_INFO, "AudioDShowInput::OnAudioData %d %d %d %d",
//                audioInfo.dwSamplingRate, audioInfo.dwChannels, audioInfo.dwBitsPerSample, lLength);
        if (ca->deviceOpener.IsAudioFormatNonPcm()) {
            if (ca->decode == nullptr) { /* having packets, create decoder now */
                obs_log(LOG_INFO, "ca->obsSource %p", ca->obsSource);
                ca->decode = new AVerMedia::FfmpegAudioDecode(ca->obsSource);
            }

            ca->decode->OnEncodedAudioData((unsigned char*)ca->buffer4Ffmpeg, ca->buffer4FfmpegSize, 0);
            return noErr;
        }
#endif // end ENABLE_FFMPEG_DECODE

         /* keep data flow even they may not be pcm data */
        struct obs_source_audio audio;
        for (UInt32 i = 0; i < 2; i++) {
            if (i < MAX_AUDIO_CHANNELS) {
                audio.data[i] = (uint8_t *)ca->buf_list->mBuffers[i].mData;
            }
        }
        audio.frames = frames;
        audio.speakers = speaker_layout::SPEAKERS_STEREO;
        audio.format = ca->format;
        audio.samples_per_sec = ca->sample_rate;
        static double factor = 0.;
        static mach_timebase_info_data_t info = {0, 0};
        if (info.numer == 0 && info.denom == 0) {
            mach_timebase_info(&info);
            factor = ((double)info.numer) / info.denom;
        }
        if (info.numer != info.denom)
            audio.timestamp = (uint64_t)(factor * ts_data->mHostTime);
        else
            audio.timestamp = ts_data->mHostTime;

        if (ca->obsSource != nullptr) {
            obs_source_output_audio(ca->obsSource, &audio);
        }
        else {
            obs_log(LOG_INFO, "ca->source is nullptr");
        }
    }

    UNUSED_PARAMETER(ignored_buffers);
    return noErr;
}

namespace AVerMedia {

CoreAudioSource::CoreAudioSource(VendorSdk* sdk, obs_data_t *settings, obs_source_t *source)
    : obsSource(source)
{
    obs_log(LOG_INFO, "CoreAudioSource obsSource %p", obsSource);
    if (os_event_init(&exit_event, OS_EVENT_TYPE_MANUAL) != 0) {
        obs_log(LOG_ERROR,
             "[coreaudio_create] failed to create "
             "semephore: %d",
             errno);
    }
    deviceOpener.SetVendorSdk(sdk);

#if defined(TEST_PROJECT)
    // device_uid = bstrdup("AppleUSBAudioEngine:AVerMedia:Live Gamer EXTREME 3:5312657500803:3");
    // device_uid = bstrdup("AppleUSBAudioEngine:AVerMedia:Live Gamer Ultra 2.1:PRODUCT123:3,4");
#else
    std::string settingId = obs_data_get_string(settings, "device_id");
    obs_log(LOG_INFO, "settingId %s", settingId.c_str());
    if (settingId.empty()) {
        obs_log(LOG_INFO, "settingId is empty");
    }
    device_uid = bstrdup(settingId.c_str());
#endif // end TEST_PROJECT


    deviceOpener.SetLogHandler([=](int log_level, const char* message){
        if (log_level == DeviceOpener::LEVEL_ERROR) {
            obs_log(LOG_ERROR, message);
        }
        else {
            obs_log(LOG_INFO, message);
        }
    });

    coreaudio_try_init();

    // log_file = fopen("/Users/mattgu/Downloads/_______capture1.raw", "wb");

    // log_file2 = fopen("/Users/mattgu/Downloads/_______capture2.raw", "wb");
}

CoreAudioSource::~CoreAudioSource()
{
    obs_log(LOG_INFO, "CoreAudioSource::~CoreAudioSource 1");
    coreaudio_shutdown();

    os_event_destroy(exit_event);

    bfree(device_name);
    bfree(device_uid);

    obs_log(LOG_INFO, "CoreAudioSource::~CoreAudioSource 2");
}

std::unordered_map<std::string, std::string> CoreAudioSource::getDevices()
{
    std::unordered_map<std::string, std::string> result;

    device_list devices = {};

    memset(&devices, 0, sizeof(device_list));

    coreaudio_enum_devices(&devices);

    for (size_t i = 0; i < devices.items.num; i++) {
        struct device_item *item = devices.items.array + i;
        if (strstr(item->value.array, "AVerMedia:Live Gamer Ultra 2.1") == NULL) {
            continue;
        }
        result.insert({std::string(item->value.array), std::string(item->name.array)});
    }

    device_list_free(&devices);
    return result;
}

void CoreAudioSource::Update(obs_data_t *settings)
{
    coreaudio_shutdown();

    bfree(device_uid);
    device_uid = bstrdup(obs_data_get_string(settings, "device_id"));

    coreaudio_try_init();
}

void CoreAudioSource::coreaudio_shutdown()
{
    deviceOpener.StopChecking();

#ifdef ENABLE_FFMPEG_DECODE
    if (decode) {
        obs_log(LOG_INFO, "CoreAudioSource::coreaudio_shutdown, delete decoder");
        delete decode;
        decode = nullptr;
    }
#endif // ENABLE_FFMPEG_DECODE

    if (reconnecting) {
        os_event_signal(exit_event);
        pthread_join(this->reconnect_thread, NULL);
        os_event_reset(exit_event);
    }

    coreaudio_uninit();

    if (unit)
        AudioComponentInstanceDispose(unit);
}

void CoreAudioSource::coreaudio_uninit()
{
    if (!au_initialized)
        return;

    if (unit) {
        coreaudio_stop();

        OSStatus stat = AudioUnitUninitialize(unit);
        ca_success(stat, this, "coreaudio_uninit", "uninitialize");

        coreaudio_remove_hooks();

        stat = AudioComponentInstanceDispose(unit);
        ca_success(stat, this, "coreaudio_uninit", "dispose");

        unit = NULL;
    }

    au_initialized = false;

    buf_list_free(buf_list);
    buf_list = NULL;

    if (buffer4Ffmpeg) {
        free(buffer4Ffmpeg);
        buffer4Ffmpeg = nullptr;
    }
}

void CoreAudioSource::coreaudio_try_init()
{
    if (!coreaudio_init()) {
        obs_log(LOG_INFO,
             "coreaudio: failed to find device "
             "uid: %s, waiting for connection",
             device_uid);

        retry_time = 2000;

        if (no_devices)
            obs_log(LOG_INFO, "coreaudio: no device found");
        else
            coreaudio_begin_reconnect();
    }
}

bool CoreAudioSource::coreaudio_init()
{
    OSStatus stat;
    DeviceOpenerParam param;

    if (au_initialized)
        return true;

    if (!find_device_id_by_uid(this))
        return false;
    if (!coreaudio_get_device_name(this))
        return false;
    if (!coreaudio_init_unit())
        return false;

    stat = enable_io(IO_TYPE_INPUT, true);
    if (!ca_success(stat, this, "coreaudio_init", "enable input io"))
        goto fail;

    stat = enable_io(IO_TYPE_OUTPUT, false);
    if (!ca_success(stat, this, "coreaudio_init", "disable output io"))
        goto fail;

    stat = set_property(unit, kAudioOutputUnitProperty_CurrentDevice,
                        SCOPE_GLOBAL, 0, &device_id,
                        sizeof(device_id));
    if (!ca_success(stat, this, "coreaudio_init", "set current device"))
        goto fail;

    if (!coreaudio_init_format())
        goto fail;
    if (!coreaudio_init_buffer())
        goto fail;
    if (!coreaudio_init_hooks())
        goto fail;

    stat = AudioUnitInitialize(unit);
    if (!ca_success(stat, this, "coreaudio_initialize", "initialize"))
        goto fail;

    if (!coreaudio_start())
        goto fail;

    obs_log(LOG_INFO, "coreaudio: Device '%s' [%" PRIu32 " Hz] initialized",
         device_name, sample_rate);

    param.name = convertCharToWString(device_name);
    param.path = convertCharToWString(device_uid);

    deviceOpener.StopChecking();
    deviceOpener.SwitchDeviceThenDetectAudioFormat(param);
    deviceOpener.StartChecking();

    return au_initialized;

fail:
    coreaudio_uninit();
    return false;
}

bool CoreAudioSource::coreaudio_init_unit()
{
    AudioComponentDescription desc = {
                                      .componentType = kAudioUnitType_Output,
                                      .componentSubType = kAudioUnitSubType_HALOutput};

    AudioComponent component = AudioComponentFindNext(NULL, &desc);
    if (!component) {
        ca_warn(this, "coreaudio_init_unit", "find component failed");
        return false;
    }

    OSStatus stat = AudioComponentInstanceNew(component, &unit);
    if (!ca_success(stat, this, "coreaudio_init_unit", "instance unit"))
        return false;

    au_initialized = true;
    return true;
}

void CoreAudioSource::coreaudio_begin_reconnect()
{
    if (reconnecting)
        return;

    int ret = pthread_create(&this->reconnect_thread, NULL, ::reconnect_thread, this);
    if (ret != 0)
        obs_log(LOG_WARNING,
             "[coreaudio_begin_reconnect] failed to "
             "create thread, error code: %d",
             ret);
}

bool CoreAudioSource::coreaudio_start()
{
    if (active)
        return true;

    OSStatus stat = AudioOutputUnitStart(unit);
    return ca_success(stat, this, "coreaudio_start", "start audio");
}

void CoreAudioSource::coreaudio_stop()
{
    if (!active)
        return;

    active = false;

    OSStatus stat = AudioOutputUnitStop(unit);
    ca_success(stat, this, "coreaudio_stop", "stop audio");
}

void CoreAudioSource::coreaudio_remove_hooks()
{
    AURenderCallbackStruct callback_info = {.inputProc = NULL,
                                            .inputProcRefCon = NULL};

    AudioObjectPropertyAddress addr = {kAudioDevicePropertyDeviceIsAlive,
                                       kAudioObjectPropertyScopeGlobal,
                                       kAudioObjectPropertyElementMain};

    AudioObjectRemovePropertyListener(device_id, &addr,
                                      notification_callback, this);

    addr.mSelector = PROPERTY_FORMATS;
    AudioObjectRemovePropertyListener(device_id, &addr,
                                      notification_callback, this);

    set_property(unit, kAudioOutputUnitProperty_SetInputCallback,
                 SCOPE_GLOBAL, 0, &callback_info, sizeof(callback_info));
}

bool CoreAudioSource::coreaudio_init_format()
{
    AudioStreamBasicDescription desc;
    AudioStreamBasicDescription inputDescription;
    OSStatus stat;
    UInt32 size;

    size = sizeof(inputDescription);
    stat = get_property(unit, kAudioUnitProperty_StreamFormat,
                        SCOPE_INPUT, BUS_INPUT, &inputDescription, &size);

    obs_log(LOG_INFO, "inputDescription mChannelsPerFrame %d", inputDescription.mChannelsPerFrame);

    if (!ca_success(stat, this, "coreaudio_init_format",
                    "get input device format"))
        return false;

    inputDescription.mFormatFlags = inputDescription.mFormatFlags & ~kAudioFormatFlagIsFloat;
    inputDescription.mFormatFlags = inputDescription.mFormatFlags | kAudioFormatFlagIsSignedInteger;
    inputDescription.mBitsPerChannel = 16;

    obs_log(LOG_INFO, "inputDescription mFormatFlags %u", (unsigned int)inputDescription.mFormatFlags);

    stat = set_property(unit, kAudioUnitProperty_StreamFormat,
                        SCOPE_INPUT, BUS_INPUT, &inputDescription, size);

    stat = get_property(unit, kAudioUnitProperty_StreamFormat,
                        SCOPE_OUTPUT, BUS_INPUT, &desc, &size);
    if (!ca_success(stat, this, "coreaudio_init_format", "get input format"))
        return false;


    desc.mChannelsPerFrame = inputDescription.mChannelsPerFrame;
    desc.mSampleRate = inputDescription.mSampleRate;

    desc.mFormatFlags = desc.mFormatFlags & ~kAudioFormatFlagIsFloat;
    desc.mFormatFlags = desc.mFormatFlags | kAudioFormatFlagIsSignedInteger;

    stat = set_property(unit, kAudioUnitProperty_StreamFormat,
                        SCOPE_OUTPUT, BUS_INPUT, &desc, size);

    if (!ca_success(stat, this, "coreaudio_init_format", "set output format"))
        return false;

    if (desc.mFormatID != kAudioFormatLinearPCM) {
        ca_warn(this, "coreaudio_init_format", "format is not PCM");
        return false;
    }

    obs_log(LOG_INFO, "desc.mFormatFlags %u", (unsigned int)desc.mFormatFlags);
    obs_log(LOG_INFO, "desc.mBitsPerChannel %u", (unsigned int)desc.mBitsPerChannel);

    format = convert_ca_format(desc.mFormatFlags, desc.mBitsPerChannel);
    if (format == AUDIO_FORMAT_UNKNOWN) {
        ca_warn(this, "coreaudio_init_format",
                "unknown format flags: "
                "%u, bits: %u",
                (unsigned int)desc.mFormatFlags,
                (unsigned int)desc.mBitsPerChannel);
        return false;
    }

    sample_rate = (uint32_t)desc.mSampleRate;

    return true;
}

bool CoreAudioSource::coreaudio_init_buffer()
{
    UInt32 bufferSizeFrames;
    UInt32 bufferSizeBytes;
    UInt32 propertySize;
    OSStatus err = noErr;

    propertySize = sizeof(bufferSizeFrames);
    err = get_property(unit, kAudioDevicePropertyBufferFrameSize,
                       SCOPE_GLOBAL, 0, &bufferSizeFrames, &propertySize);

    if (!ca_success(err, this, "coreaudio_init_buffer",
                    "get buffer frame size")) {
        return false;
    }

    bufferSizeBytes = bufferSizeFrames * sizeof(Float32);

    AudioStreamBasicDescription streamDescription;
    propertySize = sizeof(streamDescription);
    err = get_property(unit, kAudioUnitProperty_StreamFormat,
                       SCOPE_OUTPUT, 1, &streamDescription, &propertySize);

    if (!ca_success(err, this, "coreaudio_init_buffer",
                    "get stream format")) {
        return false;
    }

    Float64 rate = 0.0;
    propertySize = sizeof(Float64);
    AudioObjectPropertyAddress propertyAddress = {
                                                  kAudioDevicePropertyNominalSampleRate,
                                                  kAudioObjectPropertyScopeGlobal,
                                                  kAudioObjectPropertyElementMain};

    err = AudioObjectGetPropertyData(device_id, &propertyAddress, 0,
                                     NULL, &propertySize, &rate);

    if (!ca_success(err, this, "coreaudio_init_buffer",
                    "get input sample rate")) {
        return false;
    }

    streamDescription.mSampleRate = rate;

    int bufferPropertySize =
        offsetof(AudioBufferList, mBuffers[0]) +
        (sizeof(AudioBuffer) * streamDescription.mChannelsPerFrame);

    AudioBufferList *inputBuffer =
        (AudioBufferList *)bmalloc(bufferPropertySize);
    inputBuffer->mNumberBuffers = streamDescription.mChannelsPerFrame;

    for (UInt32 i = 0; i < inputBuffer->mNumberBuffers; i++) {
        inputBuffer->mBuffers[i].mNumberChannels = 1;
        inputBuffer->mBuffers[i].mDataByteSize = bufferSizeBytes;
        inputBuffer->mBuffers[i].mData = bmalloc(bufferSizeBytes);
    }

    obs_log(LOG_INFO, "inputBuffer->mNumberBuffers %d", inputBuffer->mNumberBuffers);
    obs_log(LOG_INFO, "bufferSizeBytes %d", bufferSizeBytes);

    buffer4FfmpegSize = bufferSizeBytes;
    buffer4Ffmpeg = (int16_t *)malloc(bufferSizeBytes);
    memset(buffer4Ffmpeg, 0, bufferSizeBytes);

    buf_list = inputBuffer;
    return true;

}

bool CoreAudioSource::coreaudio_init_hooks()
{
    OSStatus stat;
    AURenderCallbackStruct callback_info = {.inputProc = input_callback,
                                            .inputProcRefCon = this};

    stat = add_listener(this, kAudioDevicePropertyDeviceIsAlive);
    if (!ca_success(stat, this, "coreaudio_init_hooks",
                    "set disconnect callback"))
        return false;

    stat = add_listener(this, PROPERTY_FORMATS);
    if (!ca_success(stat, this, "coreaudio_init_hooks",
                    "set format change callback"))
        return false;

    stat = set_property(unit, kAudioOutputUnitProperty_SetInputCallback,
                        SCOPE_GLOBAL, 0, &callback_info,
                        sizeof(callback_info));
    if (!ca_success(stat, this, "coreaudio_init_hooks", "set input callback"))
        return false;

    return true;
}

bool CoreAudioSource::enable_io(coreaudio_io_type type, bool enable)
{
    UInt32 enable_int = enable;
    return set_property(unit, kAudioOutputUnitProperty_EnableIO,
                        (type == IO_TYPE_INPUT) ? SCOPE_INPUT
                                                : SCOPE_OUTPUT,
                        (type == IO_TYPE_INPUT) ? BUS_INPUT
                                                : BUS_OUTPUT,
                        &enable_int, sizeof(enable_int));
}

} // namespace AVerMedia
