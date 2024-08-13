#pragma once

#include "AVerMediaExports.h"

namespace AVerMedia
{

class AVERMEDIA_API VendorSdk
{
    struct VendorSdkPrivate;
public:
    VendorSdk(const char* libPath = nullptr);
    ~VendorSdk();

    enum VendorSdkEnum {
        ERROR_OK = 0,
        CONNECT_TYPE_USB_UVC = 2,
    };

    int initialize();
    int uninitialize();
    int setDevice(const char* sn, const char* uvcName, int vid, int pid);
    int setPort(int connect, const char* name);
    int closePort();
    int getAudioFormat(int* audioFormat);
    int getNonPcmOnOff(unsigned int* bEnable);
    int setNonPcmOnOff(int bEnable);
    int getSerialNum(unsigned char* rd_buff, unsigned int rd_length, unsigned int *resultLen);

private:
    VendorSdkPrivate* m_impl = nullptr;
};

}
