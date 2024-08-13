#pragma once

#include <obs.h>
#include <memory>

namespace AVerMedia {

struct ffmpeg_decode;

class FfmpegAudioDecode
{

public:
    FfmpegAudioDecode(obs_source_t* source);
    ~FfmpegAudioDecode();

    void OnEncodedAudioData(unsigned char *data, size_t size, long long ts);
    void SetEnabled(bool enabled);
    void Reset();

private:
    bool decode_valid();

private:
    std::unique_ptr<ffmpeg_decode> decode;
};

} // namespace AVerMedia
