#pragma once

#include "windows.h"

#include <obs.hpp>
#include <util/windows/WinHandle.hpp>

#include "AVerMediaDeviceOpener.h"
#include "AVerMediaAudioDevice.h"

class CriticalSection {
    CRITICAL_SECTION mutex;

public:
    inline CriticalSection() { InitializeCriticalSection(&mutex); }
    inline ~CriticalSection() { DeleteCriticalSection(&mutex); }

    inline operator CRITICAL_SECTION *() { return &mutex; }
};

class CriticalScope {
    CriticalSection &mutex;

    CriticalScope() = delete;
    CriticalScope &operator=(CriticalScope &cs) = delete;

public:
    inline CriticalScope(CriticalSection &mutex_) : mutex(mutex_)
    {
        EnterCriticalSection(mutex);
    }

    inline ~CriticalScope() { LeaveCriticalSection(mutex); }
};

namespace AVerMedia
{

class FfmpegAudioDecode;
class AudioDShowInput : public AudioDealer
{
public:
    enum class Action {
        None,
        Activate,
        ActivateBlock,
        Deactivate,
        Shutdown
    };

    AudioDShowInput(AVerMedia::VendorSdk* sdk, obs_data_t* settings, obs_source_t* source);
    ~AudioDShowInput();

    void Update(obs_data_t* settings);
    void Activate();
    void Deactivate();
    void PrepareShutDown();
    void SetActive(bool active);

    bool Activate(obs_data_t *settings);
    void QueueAction(Action action);
    void QueueActivate(obs_data_t* settings);

    void DShowLoop();
    BOOL OnAudioData(AUDIO_SAMPLE_INFO audioInfo, BYTE* pbData, LONG lLength) override;

    bool IsActivated() const { return m_active; }

#if defined(TEST_PROJECT)
    void SetDevice(const DeviceInfo& device) {
        test_device = device;
    }
#endif

private:
    obs_source_t* obsSource = nullptr;
    AudioDevice *device = nullptr;
    bool m_active = false;
    WinHandle semaphore;
    WinHandle activated_event;
    WinHandle thread;
    CriticalSection mutex;
    std::vector<Action> actions;
    FfmpegAudioDecode* decode = nullptr;
    DeviceOpener deviceOpener;
#if defined(TEST_PROJECT)
    DeviceInfo test_device;
#endif
};

struct PropertiesData
{
    AudioDShowInput* input = nullptr;
    std::vector<DeviceInfo> audioDevices;

    bool GetDevice(DeviceInfo& device, const char* encoded_id) const;
};
} // namespace AVerMedia
