#ifndef LOGHELPER_H
#define LOGHELPER_H

#include <cstdarg>

#if defined(DSHOW_LIBRARY_EXPORTS)
    #define AVERMEDIA_API __declspec(dllexport)
#elif defined(ENABLE_AVT_DSHOW_LIBRARY)
    #define AVERMEDIA_API __declspec(dllimport)
#else
    #define AVERMEDIA_API
#endif

extern "C" {
    typedef void (*log_ptr)(const char *, va_list);
    extern void printDebug(const char *format, ...);
    extern void printError(const char *format, ...);
    extern void printWarning(const char *format, ...);
    extern void printInfo(const char *format, ...);
}

namespace LogHelper {
    AVERMEDIA_API void setDebugHandler(log_ptr handler);
    AVERMEDIA_API void setErrorHandler(log_ptr handler);
    AVERMEDIA_API void setWarningHandler(log_ptr handler);
    AVERMEDIA_API void setInfoHandler(log_ptr handler);
} // namespace LogHelper

#endif // LOGHELPER_H
