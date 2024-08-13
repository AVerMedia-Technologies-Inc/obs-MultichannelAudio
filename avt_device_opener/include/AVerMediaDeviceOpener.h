#ifndef AVERMEDIADEVICEOPENER_H
#define AVERMEDIADEVICEOPENER_H

#include <functional>
#include <string>
#include "AVerMediaExports.h"

namespace AVerMedia
{

class VendorSdk;

struct DeviceSwitchParam
{
    std::string serialNumber;
    std::string uvcName;
    int vid = -1;
    int pid = -1;
};

struct DeviceOpenerParam
{
    std::wstring name;
    std::wstring path;

    bool isEmpty() const;

    AVERMEDIA_API DeviceSwitchParam getDeviceSwitchParam() const;
};

class AVERMEDIA_API DeviceOpener
{
    struct DeviceOpenerPrivate;
public:
    enum LogLevel {
        LEVEL_INFO,
        LEVEL_ERROR
    };

    DeviceOpener();
    ~DeviceOpener();

    void SetVendorSdk(VendorSdk* sdk);
    void SetLogHandler(const std::function<void(int, const char*)>& handler);
    void SwitchDeviceThenDetectAudioFormat(const DeviceOpenerParam& openerParam);
    bool IsAudioFormatNonPcm() const;
    void StartChecking();
    void StopChecking();

private:
    bool SwitchDevice(const DeviceOpenerParam& openerParam);
    bool DetectAudioFormatByVendorSdk();
    bool EnableNonPcmDataByVendorSdk(bool allowLog = true);

private:
    DeviceOpenerPrivate* m_impl = nullptr;
};

}

#endif // AVERMEDIADEVICEOPENER_H
