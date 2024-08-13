#pragma once

#include <dshow.h>
#include <atlcomcli.h>
#include <vector>
#include <string>

#include "AvtDShowAudioSampleStruct.h"

#if defined(DSHOW_LIBRARY_EXPORTS)
    #define AVERMEDIA_API __declspec(dllexport)
#elif defined(ENABLE_AVT_DSHOW_LIBRARY)
    #define AVERMEDIA_API __declspec(dllimport)
#else
    #define AVERMEDIA_API
#endif

namespace AVerMedia
{
    struct DeviceInfo {
        std::wstring name;
        std::wstring path;
    };

    class AudioDealer
    {        
    public:
        virtual ~AudioDealer() {};
        virtual BOOL OnAudioData(AUDIO_SAMPLE_INFO audioInfo, BYTE* pbData, LONG lLength) = 0;
    };    

    class AudioWdmCaptureDevice
    {
    public:
        AudioWdmCaptureDevice();
        ~AudioWdmCaptureDevice();

        HRESULT Initialize();
        HRESULT BuildCaptureFilter(const std::wstring &deviceName, const std::wstring &devicePath);
        HRESULT BuildCallbackFilter();
        HRESULT BuildNullRendererFilter();
        HRESULT ConnectGraph();
        HRESULT DisconnectFilters();
        HRESULT SetCallback(AudioDealer* audioDealer);
        HRESULT Run();
        HRESULT Stop();

    private:
        CComPtr<IGraphBuilder> pGraph;
        CComPtr<IMediaControl> pControl;
        CComPtr<IBaseFilter> pInputDevice;
        CComPtr<IBaseFilter> pAvtCallbackFilter;
        CComPtr<IBaseFilter> pNullRenderer;
        CComPtr<IMoniker> pDeviceMonik;
        bool coInitialized = false;
    };

    class AVERMEDIA_API AudioDevice
    {
    public:
        AudioDevice();
        ~AudioDevice();

        bool ResetGraph();
        void ShutdownGraph();
        bool ConnectFilters();
        bool UpdateDevice(const std::wstring &deviceName, const std::wstring &devicePath);
        bool SetCallback(AudioDealer* audioDealer);
        bool Start();
        bool Stop();

        static void GetDeviceList(std::vector<DeviceInfo> &devices);

    private:
        AudioWdmCaptureDevice* context = nullptr;
    };
} // namespace AVerMedia


