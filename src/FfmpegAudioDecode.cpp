#include "FfmpegAudioDecode.hpp"

#include <plugin-support.h>
#include <util/threading.h>
#include <util/deque.h>
#include <util/platform.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#include <util/platform.h>
#include <util/dstr.hpp>

#define AVIO_BUFFER_SIZE 2560
//#define COPY_AUDIO_DATA 1

using namespace AVerMedia;

struct AVerMedia::ffmpeg_decode
{
    pthread_t thread;
    struct deque packets;
    pthread_mutex_t mutex;

    bool kill = false;
    bool streamOpen = false;
    bool streamFound = false;
    bool enabled = true;

    AVIOContext *ioContext = nullptr;
    unsigned char * avio_ctx_buffer = nullptr;
    AVFormatContext *formatContext = nullptr;

    const AVCodec *codec = nullptr;
    AVCodecContext *decoder = nullptr;
    AVFrame *frame = nullptr;
    //AVPacket *pkt = nullptr;

    uint8_t *packet_buffer = nullptr;
    size_t packet_size = 0;

    obs_source_t* obsSource = nullptr;
    obs_source_audio audio = {};
    uint64_t base_time = 0;
};


void ffmpeg_log(void *bla, int level, const char *msg, va_list args)
{
    DStr str;
    if (level == AV_LOG_WARNING) {
        dstr_copy(str, "warning: ");
    } else if (level == AV_LOG_ERROR) {
        /* only print first of this message to avoid spam */
        static bool suppress_app_field_spam = false;
        if (strcmp(msg, "unable to decode APP fields: %s\n") == 0) {
            if (suppress_app_field_spam)
                return;

            suppress_app_field_spam = true;
        }

        dstr_copy(str, "error:   ");
    } else if (level < AV_LOG_ERROR) {
        dstr_copy(str, "fatal:   ");
    } else {
        return;
    }

    dstr_cat(str, msg);
    if (dstr_end(str) == '\n')
        dstr_resize(str, str->len - 1);

    blogva(LOG_WARNING, str, args);
    av_log_default_callback(bla, level, msg, args);
}

// https://shigure624.github.io/posts/%E4%BA%86%E8%A7%A3ffmpeg%E9%94%99%E8%AF%AF%E7%A0%81.html
static void print_ffmpeg_error(int error, const char* message = nullptr)
{
    if (error < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        int errbuf_size = sizeof(errbuf);
        if (av_strerror(error, errbuf, errbuf_size) == 0) {
            if (message) {
                obs_log(LOG_ERROR, "%s: %s", message, errbuf);
            } else {
                obs_log(LOG_ERROR, errbuf);
            }
        }
    }
}

struct pkg_data {
	//AUDIO_SAMPLE_INFO audioInfo;
    uint8_t* data;
    int size;
};

static int read_packet(void *opaque, uint8_t *buf, int buf_size)
{
	auto decode = (ffmpeg_decode *)opaque;
	//obs_log(LOG_INFO, "read_packet: buf_size=%d, %d", buf_size, decode->packets.size);
    //if (decode->kill) return AVERROR_EOF;
    //if (decode->packets.size <= 0) return 0;//AVERROR_UNKNOWN;

    while(true) { // wait for data
        pthread_mutex_lock(&decode->mutex);
		bool kill = decode->kill;
		size_t packageSize = decode->packets.size;
        pthread_mutex_unlock(&decode->mutex);

        if (kill) return AVERROR_EOF;
        if (packageSize > 0) break;
        os_sleep_ms(1);
    }

	pkg_data *pkt;

    pthread_mutex_lock(&decode->mutex);
	deque_pop_front(&decode->packets, &pkt, sizeof(pkt));
    pthread_mutex_unlock(&decode->mutex);

    int size = pkt->size;
    if (pkt->size > buf_size) size = buf_size;
    memcpy(buf, pkt->data, size);
    //obs_log(LOG_INFO, "  read_packet: size=%d (%d)", size, decode->packets.size);
#ifdef COPY_AUDIO_DATA
    bfree(pkt->data);
#endif // COPY_AUDIO_DATA
	delete pkt;
    return size; //AVERROR_EOF;
}

static inline enum audio_format convert_sample_format(int f)
{
	switch (f) {
	case AV_SAMPLE_FMT_U8:
		return AUDIO_FORMAT_U8BIT;
	case AV_SAMPLE_FMT_S16:
		return AUDIO_FORMAT_16BIT;
	case AV_SAMPLE_FMT_S32:
		return AUDIO_FORMAT_32BIT;
	case AV_SAMPLE_FMT_FLT:
		return AUDIO_FORMAT_FLOAT;
	case AV_SAMPLE_FMT_U8P:
		return AUDIO_FORMAT_U8BIT_PLANAR;
	case AV_SAMPLE_FMT_S16P:
		return AUDIO_FORMAT_16BIT_PLANAR;
	case AV_SAMPLE_FMT_S32P:
		return AUDIO_FORMAT_32BIT_PLANAR;
	case AV_SAMPLE_FMT_FLTP:
		return AUDIO_FORMAT_FLOAT_PLANAR;
	default:;
	}

	return AUDIO_FORMAT_UNKNOWN;
}

static inline enum speaker_layout convert_speaker_layout(uint8_t channels)
{
	switch (channels) {
	case 0:
		return SPEAKERS_UNKNOWN;
	case 1:
		return SPEAKERS_MONO;
	case 2:
		return SPEAKERS_STEREO;
	case 3:
		return SPEAKERS_2POINT1;
	case 4:
		return SPEAKERS_4POINT0;
	case 5:
		return SPEAKERS_4POINT1;
	case 6:
		return SPEAKERS_5POINT1;
	case 8:
		return SPEAKERS_7POINT1;
	default:
		return SPEAKERS_UNKNOWN;
	}
}

static void ffmpeg_init_avio(ffmpeg_decode *decode)
{
    if (decode->avio_ctx_buffer == nullptr) {
        decode->avio_ctx_buffer = (unsigned char *)av_malloc(AVIO_BUFFER_SIZE);
    }
    decode->ioContext = avio_alloc_context(decode->avio_ctx_buffer, AVIO_BUFFER_SIZE, 0, decode,
                                           &read_packet, nullptr, nullptr);

    if (decode->formatContext == nullptr) {
        decode->formatContext = avformat_alloc_context();
    }

    decode->formatContext->pb = decode->ioContext;

    if (decode->frame == nullptr) {
        decode->frame = av_frame_alloc();
    }
}

static bool ffmpeg_open_avio(ffmpeg_decode *decode)
{
    const AVInputFormat *format = nullptr;
    int ret;

#if 0
    obs_log(LOG_INFO, "av_probe_input_buffer2");
    ret = av_probe_input_buffer2(decode->ioContext, &format, "", nullptr, 0, 0);
    if (ret < 0) {
        print_ffmpeg_error(ret, "av_probe_input_buffer2");
        //return false;
        //obs_log(LOG_INFO, "av_probe_input_buffer2 no format");
        format = av_find_input_format("spdif"); // use spdif directly
    }
#else
    format = av_find_input_format("spdif"); // use spdif directly
#endif

    // https://ffmpeg.org/doxygen/trunk/structAVFormatContext.html#a4d860662c014f88277c8f20e238fa694
    decode->formatContext->max_analyze_duration = 2 * AV_TIME_BASE; // microseconds
    obs_log(LOG_INFO, "avformat_open_input");
    ret = avformat_open_input(&decode->formatContext, nullptr, format, nullptr);
    //obs_log(LOG_INFO, "avformat_open_input %d", ret);
    if (format) obs_log(LOG_INFO, "av_probe_input_buffer2 %s", format->name);
    if (ret < 0) print_ffmpeg_error(ret, "avformat_open_input");
    return ret == 0;
}

static int ffmpeg_find_stream(ffmpeg_decode *decode)
{
    int ret;
    obs_log(LOG_INFO, "avformat_find_stream_info");
    // need to parse streams to get audio stream
    ret = avformat_find_stream_info(decode->formatContext, nullptr);
    //obs_log(LOG_INFO, "avformat_find_stream_info %d", ret);
    if (ret < 0) {
        print_ffmpeg_error(ret, "avformat_find_stream_info");
        return ret;
    }

    obs_log(LOG_INFO, "av_find_best_stream audio");
    ret = av_find_best_stream(decode->formatContext, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (ret < 0) {
        print_ffmpeg_error(ret, "av_find_best_stream audio");
    } else {
        auto stream = decode->formatContext->streams[ret];
        obs_log(LOG_INFO, "audio stream channels = %d", stream->codecpar->ch_layout.nb_channels);

        auto id = stream->codecpar->codec_id;
        //obs_log(LOG_INFO, "avcodec_find_decoder %d", id);
        auto codec = avcodec_find_decoder(id);
        if (codec) obs_log(LOG_INFO, "avcodec_find_decoder: %s", codec->name);
        decode->decoder = avcodec_alloc_context3(codec);
        //obs_log(LOG_INFO, "avcodec_parameters_to_context");
        ret = avcodec_parameters_to_context(decode->decoder, stream->codecpar);
        if (ret < 0) {
            print_ffmpeg_error(ret, "avcodec_parameters_to_context");
            return ret;
        }

        //obs_log(LOG_INFO, "avcodec_open2");
        ret = avcodec_open2(decode->decoder, codec, nullptr);
        if (ret < 0) {
            print_ffmpeg_error(ret, "avcodec_open2");
        }

        //obs_log(LOG_INFO, "cached packets = %d", decode->packets.size);
    }
    return ret;
}

static int ffmpeg_decode_audio(ffmpeg_decode *decode, bool& got_frame)
{
    int ret;
    AVPacket *pkt = av_packet_alloc();
    ret = av_read_frame(decode->formatContext, pkt);
    if (ret < 0) {
        print_ffmpeg_error(ret, "av_read_frame");
        return ret;
    }
    //obs_log(LOG_INFO, "av_read_frame %d %d", pkt->pts, pkt->size);

    ret = avcodec_send_packet(decode->decoder, pkt);
    av_packet_free(&pkt);
    if (ret < 0) {
        print_ffmpeg_error(ret, "avcodec_send_packet");
        return ret;
    }

    ret = avcodec_receive_frame(decode->decoder, decode->frame);
    if (ret < 0) {
        print_ffmpeg_error(ret, "avcodec_receive_frame");
        return ret;
    }
    got_frame = ret == 0;
    //obs_log(LOG_INFO, "avcodec_receive_frame pkt-size=%d, samples=%d",
    //        decode->frame->pkt_size, decode->frame->nb_samples);
    return ret;
}

static void ffmpeg_push_frame(ffmpeg_decode *decode)
{
    //if (decode->obsSource == nullptr) return;

    //obs_source_audio audio = {};
    for (size_t i = 0; i < MAX_AV_PLANES; i++)
        decode->audio.data[i] = decode->frame->data[i];

    decode->audio.samples_per_sec = decode->frame->sample_rate;
    //audio.format = AUDIO_FORMAT_FLOAT;
    //audio.speakers = SPEAKERS_5POINT1;
    decode->audio.format = convert_sample_format(decode->frame->format);
    decode->audio.speakers =
        convert_speaker_layout((uint8_t)decode->frame->ch_layout.nb_channels);
    decode->audio.frames = decode->frame->nb_samples;

#if defined(WIN32)
    decode->audio.timestamp = os_gettime_ns();
    decode->audio.timestamp -= util_mul_div64(decode->frame->nb_samples, UINT64_C(1000000000),
                                              decode->frame->sample_rate);
#else
    // Note: don't merge with the above code yet, or it will cause audio lagging
    //decode->audio.timestamp = decode->frame->pts;
    if (decode->base_time == 0) {
        decode->base_time = os_gettime_ns();
    }
    decode->audio.timestamp = os_gettime_ns() - decode->base_time;
#endif // WIN32

    if (decode->obsSource) {
        obs_source_output_audio(decode->obsSource, &decode->audio);
    } else {
        obs_log(LOG_INFO, "obs_source_output_audio %lu %d %d",
                decode->audio.timestamp, decode->audio.frames, decode->frame->ch_layout.nb_channels);
    }
}

static void ffmpeg_decode_free(ffmpeg_decode *decode)
{
    if (decode == nullptr) return;

    if (decode->decoder) {
        avcodec_free_context(&decode->decoder);
        decode->decoder = nullptr;
    }
    if (decode->frame) {
        av_frame_free(&decode->frame);
        decode->frame = nullptr;
    }
    if (decode->packet_buffer) {
        bfree(decode->packet_buffer);
        decode->packet_buffer = nullptr;
    }

    if (decode->formatContext) {
        decode->formatContext->pb = nullptr;
        // release context when stream is opened. or it may cause app crash
        if (decode->streamOpen) {
            avformat_close_input(&decode->formatContext);
        }
        decode->formatContext = nullptr;
    }

    if (decode->ioContext) {
        // `avio_ctx_buffer` will be freed in `avio_context_free`
        avio_context_free(&decode->ioContext);
        decode->ioContext = nullptr;
    }

    decode->base_time = 0;
}

static void clean_buffer_packets(ffmpeg_decode *decode)
{
    // release all buffered data
    while (decode->packets.size) {
        pkg_data *pkt;

        pthread_mutex_lock(&decode->mutex);
        deque_pop_front(&decode->packets, &pkt, sizeof(pkt));
        pthread_mutex_unlock(&decode->mutex);

#ifdef COPY_AUDIO_DATA
        bfree(pkt->data);
#endif // COPY_AUDIO_DATA
        delete pkt;
    }
    //circlebuf_free(&decode->packets);
}

static void* ffmpeg_decode_thread(void *opaque)
{
    os_set_thread_name("ffmpeg_decode_thread");
    //auto tid = GetCurrentThreadId();
    //obs_log(LOG_INFO, "ffmpeg_decode_thread %d ENTER", tid);
    auto decode = (ffmpeg_decode *)opaque;

    //ffmpeg_prepare_avio(decode);
    int ret;
    while (true) {
		if (decode == nullptr) break;
        bool kill = false;
        pthread_mutex_lock(&decode->mutex);
		kill = decode->kill;
        pthread_mutex_unlock(&decode->mutex);
        if (kill) break;

        if (decode->enabled == false) {
            os_sleep_ms(2);
            continue;
        }
		
		if (!decode->streamOpen) {
            decode->streamOpen = ffmpeg_open_avio(decode);
		}
		
        if (decode->streamOpen && !decode->streamFound) {
            ret = ffmpeg_find_stream(decode);
            decode->streamFound = ret == 0;
#if !defined(WIN32)
            ret = avformat_flush(decode->formatContext);
            obs_log(LOG_INFO, "avformat_flush %d", ret);
#endif // WIN32
		}

        if (decode->streamOpen && decode->streamFound) {
            bool got_frame = false;
            ret = ffmpeg_decode_audio(decode, got_frame);
            if (ret < 0) {
                //print_ffmpeg_error(ret, "ffmpeg_decode_audio");
            } else if (got_frame) {
                ffmpeg_push_frame(decode);
            }
        }
    }

    //ffmpeg_decode_free(decode);

    //obs_log(LOG_INFO, "ffmpeg_decode_thread %d LEAVE", tid);
    return nullptr;
}

FfmpegAudioDecode::FfmpegAudioDecode(obs_source_t* source)
    : decode(std::make_unique<ffmpeg_decode>())
{
    if (pthread_mutex_init(&decode->mutex, NULL) != 0) {
        obs_log(LOG_WARNING, "FfmpegAudioDecode: Failed to init mutex");
        return;
    }

    av_log_set_level(AV_LOG_INFO);
    av_log_set_callback(ffmpeg_log);
    avformat_network_init();

    pthread_mutex_lock(&decode->mutex);
    deque_init(&decode->packets);
    decode->kill = false;
    pthread_mutex_unlock(&decode->mutex);

    decode->obsSource = source;

    ffmpeg_init_avio(decode.get());
    pthread_create(&decode->thread, nullptr, ffmpeg_decode_thread, decode.get());
}

FfmpegAudioDecode::~FfmpegAudioDecode()
{
    //auto tid = GetCurrentThreadId();
    //obs_log(LOG_INFO, "FfmpegAudioDecode::~FfmpegAudioDecode() stop thread %d", tid);
    pthread_mutex_lock(&decode->mutex);
    decode->kill = true;
    pthread_mutex_unlock(&decode->mutex);
    pthread_join(decode->thread, nullptr);
    obs_log(LOG_INFO, "FfmpegAudioDecode::~FfmpegAudioDecode() stop thread done");

    clean_buffer_packets(decode.get());
    pthread_mutex_lock(&decode->mutex);
    deque_free(&decode->packets);
    pthread_mutex_unlock(&decode->mutex);

    ffmpeg_decode_free(decode.get());
    decode->obsSource = nullptr;

	avformat_network_deinit();

    pthread_mutex_destroy(&decode->mutex);
}

#if 0
static inline void copy_data(struct ffmpeg_decode *decode, uint8_t *data, size_t size)
{
	size_t new_size = size + AV_INPUT_BUFFER_PADDING_SIZE;
	//obs_log(LOG_INFO, "copy_data %d %d", size, new_size);
	
	if (decode->packet_size < new_size) {
		decode->packet_buffer =
			(uint8_t *)brealloc(decode->packet_buffer, new_size);
		decode->packet_size = new_size;
	}
	
	memset(decode->packet_buffer + size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
	memcpy(decode->packet_buffer, data, size);
}
#endif

void FfmpegAudioDecode::OnEncodedAudioData(unsigned char *data, size_t size, long long /*ts*/)
{
    //obs_log(LOG_INFO, "OnAudioData %d", size);
    if (size == 0) {
        obs_log(LOG_WARNING, "OnAudioData empty");
        return; // don't push empty data
    }
    if (decode->enabled == false) {
        return; // drop data when decode disabled
    }

	pkg_data* pkt = new pkg_data;
    pkt->size = (int)size;
#ifdef COPY_AUDIO_DATA
    pkt->data = (uint8_t *)bmalloc(size);
    memcpy(pkt->data, data, size);
#else
    pkt->data = data;
#endif // COPY_AUDIO_DATA

    pthread_mutex_lock(&decode->mutex);
    deque_push_back(&decode->packets, &pkt, sizeof(pkt));
    pthread_mutex_unlock(&decode->mutex);
    //obs_log(LOG_INFO, "OnAudioData %d %d", size, decode->packets.size);

#if 0
	int ret;
    if (decode->decoder == nullptr) {
        decode->codec = avcodec_find_decoder(AV_CODEC_ID_AC3);
        decode->decoder = avcodec_alloc_context3(decode->codec);
		
        ret = avcodec_open2(decode->decoder, decode->codec, NULL);
		if (ret < 0) {
			print_ffmpeg_error(ret);
			if (decode.decoder) {
                avcodec_free_context(&decode->decoder);
			}
		} else {
			obs_log(LOG_INFO, "avcodec_open2 ready");
		}
		return TRUE;
	}
	
    copy_data(&decode, data, size);
    if (!decode->frame) {
        decode->frame = av_frame_alloc();
	}
    if (decode->frame) {
		if (pbData && lLength) {
			AVPacket *packet = av_packet_alloc();
            packet->data = decode->packet_buffer;
			packet->size = (int)lLength;

            ret = avcodec_send_packet(decode->decoder, packet);
			//obs_log(LOG_INFO, "avcodec_send_packet %d", ret);

			av_packet_free(&packet);
		}
		if (ret == 0) {
            ret = avcodec_receive_frame(decode->decoder, decode->frame);
			obs_log(LOG_INFO, "avcodec_receive_frame %d", ret);
		} else {
			obs_log(LOG_INFO, "avcodec_send_packet wrong");
			print_ffmpeg_error(ret);
		}
        return; // ret == 0;
	}
#endif
}

bool FfmpegAudioDecode::decode_valid()
{
    return decode->decoder != nullptr;
}

void FfmpegAudioDecode::SetEnabled(bool enabled)
{
    if (enabled != decode->enabled) {
        clean_buffer_packets(decode.get()); // clear all data when state changed
    }
    decode->enabled = enabled;
}

void FfmpegAudioDecode::Reset()
{
    obs_log(LOG_INFO, "FfmpegAudioDecode::Reset() stop decode thread");
    pthread_mutex_lock(&decode->mutex);
    decode->kill = true;
    pthread_mutex_unlock(&decode->mutex);
    pthread_join(decode->thread, nullptr);
    obs_log(LOG_INFO, "FfmpegAudioDecode::Reset() stop decode thread done");

    clean_buffer_packets(decode.get());
    //circlebuf_free(&decode->packets);
    ffmpeg_decode_free(decode.get());

    //circlebuf_init(&decode->packets);
    pthread_mutex_lock(&decode->mutex);
    decode->kill = false;
    pthread_mutex_unlock(&decode->mutex);
    ffmpeg_init_avio(decode.get());
    pthread_create(&decode->thread, nullptr, ffmpeg_decode_thread, decode.get());
}
