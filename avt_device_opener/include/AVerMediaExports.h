#ifndef AVERMEDIAEXPORTS_H
#define AVERMEDIAEXPORTS_H

#ifdef MACOS
    #define AVERMEDIA_API
#elif defined(DEVICE_OPENER_LIBRARY_EXPORTS)
    #define AVERMEDIA_API __declspec(dllexport)
#elif defined(ENABLE_AVT_DEVICE_OPENER_LIBRARY)
    #define AVERMEDIA_API __declspec(dllimport)
#else
    #define AVERMEDIA_API
#endif

#endif // AVERMEDIAEXPORTS_H
