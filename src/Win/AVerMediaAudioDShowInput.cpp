#include "AVerMediaAudioDShowInput.h"

#include <plugin-support.h>
#include <util/platform.h>
#include <util/threading.h>
#include "encode-dstr.hpp"

#define UNUSED(param) (void)param;
#define AUDIO_DEVICE_ID   "audio_device_id"

#ifdef ENABLE_FFMPEG_DECODE
#include "FfmpegAudioDecode.hpp"
#endif // ENABLE_FFMPEG_DECODE

static DWORD CALLBACK DShowThread(LPVOID ptr)
{
    AVerMedia::AudioDShowInput *audioSource = (AVerMedia::AudioDShowInput *)ptr;

    os_set_thread_name("AVerMedia AudioDShowInput THREAD");

    auto hr = CoInitialize(nullptr);
    if (FAILED(hr)) {
		obs_log(LOG_ERROR, "DShowThread CoInitialize failed with error %08X", hr);
		return 1;
	}
    audioSource->DShowLoop();
    CoUninitialize();
    return 0;
}

static inline void ProcessMessages()
{
    MSG msg;
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

namespace AVerMedia
{
    AudioDShowInput::AudioDShowInput(VendorSdk *sdk, obs_data_t* settings, obs_source_t* source)
        : obsSource(source), device(new AVerMedia::AudioDevice)
    {
        deviceOpener.SetVendorSdk(sdk);
        deviceOpener.SetLogHandler([=](int log_level, const char* message){
            if (log_level == DeviceOpener::LEVEL_ERROR) {
                obs_log(LOG_ERROR, message);
            }
            else {
                obs_log(LOG_DEBUG, message);
            }
        });

        semaphore = CreateSemaphore(nullptr, 0, 0x7FFFFFFF, nullptr);
        if (!semaphore)
            throw "Failed to create semaphore";

        activated_event = CreateEvent(nullptr, false, false, nullptr);
        if (!activated_event)
            throw "Failed to create activated_event";

        thread = CreateThread(nullptr, 0, DShowThread, this, 0, nullptr);
        if (!thread)
            throw "Failed to create thread";

        if (obs_data_get_bool(settings, "active")) {
            bool showing = obs_source_showing(source);
            if (showing) {
                QueueAction(Action::Activate);
            }
            m_active = true;
        }

        DWORD dwThreadID = GetCurrentThreadId();
        obs_log(LOG_DEBUG, "AudioDShowInput::AudioDShowInput, thread id: %d", dwThreadID);
    }

    AudioDShowInput::~AudioDShowInput()
    {
        obs_log(LOG_DEBUG, "AudioDShowInput::~AudioDShowInput 1");

        if (device) {
            obs_log(LOG_DEBUG, "AudioDShowInput::~AudioDShowInput, delete device");
            device->Stop();
            delete device;
            device = nullptr;
        }

#ifdef ENABLE_FFMPEG_DECODE
        if (decode) {
            obs_log(LOG_DEBUG, "AudioDShowInput::~AudioDShowInput, delete decoder");
            delete decode;
            decode = nullptr;
        }
#endif // ENABLE_FFMPEG_DECODE

        {
            CriticalScope scope(mutex);
            actions.resize(1);
            actions[0] = Action::Shutdown;
        }

        ReleaseSemaphore(semaphore, 1, nullptr);

        WaitForSingleObject(thread, INFINITE);

        obs_log(LOG_DEBUG, "AudioDShowInput::~AudioDShowInput 2");
    }

    void AudioDShowInput::Update(obs_data_t* settings)
    {
        if (m_active) {
            QueueActivate(settings);
        }
    }

    void AudioDShowInput::Activate()
    {
        if (device) {
            obs_log(LOG_DEBUG, "AudioDShowInput Activate Start");
            device->Start();
        }
#ifdef ENABLE_FFMPEG_DECODE
        if (decode) {
            obs_log(LOG_DEBUG, "AudioDShowInput Deactivate, enable decoder");
            decode->SetEnabled(true);
        }
#endif // ENABLE_FFMPEG_DECODE
    }

    void AudioDShowInput::Deactivate()
    {
        deviceOpener.StopChecking();
        if (device) {
            obs_log(LOG_DEBUG, "AudioDShowInput Deactivate, ResetGraph");
            //device->ResetGraph();
            device->Stop();
            obs_log(LOG_DEBUG, "AudioDShowInput Deactivate, ResetGraph END");
        }
#ifdef ENABLE_FFMPEG_DECODE
        if (decode) {
            obs_log(LOG_DEBUG, "AudioDShowInput Deactivate, disable decoder");
            decode->SetEnabled(false);
        }
#endif // ENABLE_FFMPEG_DECODE
    }

    void AudioDShowInput::PrepareShutDown()
    {
        deviceOpener.StopChecking();

        if (device) {
            device->ShutdownGraph();
        }
    }

    void AudioDShowInput::SetActive(bool active)
    {
        OBSDataAutoRelease settings = obs_source_get_settings(obsSource);
        QueueAction(active ? Action::Activate : Action::Deactivate);
        obs_data_set_bool(settings, "active", active);
        m_active = active;
    }

    bool AudioDShowInput::Activate(obs_data_t* settings)
    {
        obs_log(LOG_DEBUG, "AudioDShowInput::Activate 0");

#ifdef ENABLE_FFMPEG_DECODE
        if (decode) {
            obs_log(LOG_DEBUG, "delete old decoder");
            delete decode;
            decode = nullptr;
        }
#endif // ENABLE_FFMPEG_DECODE

        DeviceInfo info;
#if defined(TEST_PROJECT)
        info = test_device;
        obs_log(LOG_DEBUG, "AudioDShowInput::Activate, TEST_PROJECT");
        obs_log(LOG_DEBUG, "AudioDShowInput::Activate, DeviceInfo %ls, %ls", info.name.c_str(), info.path.c_str());
#else
        std::string audio_device_id = obs_data_get_string(settings, AUDIO_DEVICE_ID);
        if (!DecodeDeviceId(info.name, info.path, audio_device_id.c_str())) {
            obs_log(LOG_ERROR, "AudioDShowInput::Activate, DecodeDeviceId failed");
            return false;
        }
#endif

        deviceOpener.StopChecking();
        deviceOpener.SwitchDeviceThenDetectAudioFormat({info.name, info.path});

        obs_log(LOG_DEBUG, "AudioDShowInput::Activate 1");
        if (device == nullptr) {
            return false;
        }
        obs_log(LOG_DEBUG, "AudioDShowInput::Activate 2");
        if (!device->ResetGraph()) {
            return false;
        }
        obs_log(LOG_DEBUG, "AudioDShowInput::Activate 3");

        if (!device->UpdateDevice(info.name, info.path)) {
            return false;
        }

        obs_log(LOG_DEBUG, "AudioDShowInput::Activate 4");
        if (!device->ConnectFilters()) {
            return false;
        }           
        obs_log(LOG_DEBUG, "AudioDShowInput::Activate 5");
        if (!device->SetCallback(this)) {
            return false;
        }
        obs_log(LOG_DEBUG, "AudioDShowInput::Activate 6");
        if (!device->Start()) {
            return false;
        }
        obs_log(LOG_DEBUG, "AudioDShowInput::Activate 7");
        deviceOpener.StartChecking();

        return true;
    }

    void AudioDShowInput::QueueAction(Action action)
    {
        obs_log(LOG_DEBUG, "AudioDShowInput::QueueAction %d", action);
        actions.push_back(action);
        ReleaseSemaphore(semaphore, 1, nullptr);
    }

    void AudioDShowInput::QueueActivate(obs_data_t* settings)
    {
        bool block =
            obs_data_get_bool(settings, "synchronous_activate");
        QueueAction(block ? Action::ActivateBlock : Action::Activate);
        if (block) {
            obs_data_erase(settings, "synchronous_activate");
            WaitForSingleObject(activated_event, INFINITE);
        }
    }

    /* Always keep directshow in a single thread for a given device */
    void AudioDShowInput::DShowLoop()
    {
        obs_log(LOG_DEBUG, "AudioDShowInput::DShowLoop, thread id: %lu", GetCurrentThreadId());

        while (true) {
            DWORD ret = MsgWaitForMultipleObjects(1, &semaphore, false,
                INFINITE, QS_ALLINPUT);
            if (ret == (WAIT_OBJECT_0 + 1)) {
                ProcessMessages();
                continue;
            }
            else if (ret != WAIT_OBJECT_0) {
                break;
            }

            Action action = Action::None;
            {
                CriticalScope scope(mutex);
                if (actions.size()) {
                    action = actions.front();
                    actions.erase(actions.begin());
                }
            }

            switch (action) {
            case Action::Activate:
            case Action::ActivateBlock: {
                obs_log(LOG_DEBUG, "AudioDShowInput::DShowLoop, Action::Activate");
                OBSDataAutoRelease settings = obs_source_get_settings(obsSource);
                
                Activate(settings);

                if (action == Action::ActivateBlock) {
                    SetEvent(activated_event);
                }
                break;;
            };
            case Action::Deactivate:
                Deactivate();
                break;

            case Action::Shutdown:
                PrepareShutDown();
                return;
            case Action::None:;
            }
        }
    }

    BOOL AudioDShowInput::OnAudioData(AUDIO_SAMPLE_INFO audioInfo, BYTE* pbData, LONG lLength)
    {
#ifdef ENABLE_FFMPEG_DECODE
//        obs_log(LOG_DEBUG, "AudioDShowInput::OnAudioData %d %d %d %d",
//                audioInfo.dwSamplingRate, audioInfo.dwChannels, audioInfo.dwBitsPerSample, lLength);

        if (deviceOpener.IsAudioFormatNonPcm()) {
            if (decode == nullptr) { /* having packets, create decoder now */
                decode = new FfmpegAudioDecode(obsSource);
            }
            decode->OnEncodedAudioData(pbData, lLength, 0);
            return TRUE;
        }
#endif  // ENABLE_FFMPEG_DECODE
#ifdef TEST_PROJECT
        obs_log(LOG_DEBUG, "AudioDShowInput::OnAudioData");
#endif
#if 0
        /* keep data flow even they may not be pcm data */
        UINT32 frames = lLength / sizeof(uint32_t);
        uint32_t sampleRate = audioInfo.dwSamplingRate;

        obs_source_audio data = {};
        data.data[0] = (const uint8_t*)pbData;
        data.frames = (uint32_t)frames;
        data.samples_per_sec = sampleRate;

        if (audioInfo.dwChannels == 2) {
            data.speakers = SPEAKERS_STEREO;
        }
        if (audioInfo.dwBitsPerSample == 16) {
            data.format = AUDIO_FORMAT_16BIT;
        }

        data.timestamp = os_gettime_ns();
        data.timestamp -= util_mul_div64(frames, UINT64_C(1000000000), sampleRate);

        if (obsSource) {
            obs_source_output_audio(obsSource, &data);
        }
        else {
            //obs_log(LOG_DEBUG, "AudioDShowInput::OnAudioData");
        }
#endif
        return TRUE;
    }

    bool PropertiesData::GetDevice(DeviceInfo& device, const char* encoded_id) const
    {        
        DeviceInfo deviceId;
        DecodeDeviceId(deviceId.name, deviceId.path, encoded_id);

        for (const DeviceInfo& curDevice : audioDevices) {
            if (deviceId.name == curDevice.name &&
                deviceId.path == curDevice.path) {
                device = curDevice;
                return true;
            }
        }

        return false;
    }
} // namespace AVerMedia
