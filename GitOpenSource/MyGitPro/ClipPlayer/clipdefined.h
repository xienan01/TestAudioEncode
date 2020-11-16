#ifndef CLIPDEFINED_H
#define CLIPDEFINED_H

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#include <libavutil/channel_layout.h>
#include <libavutil/audio_fifo.h>

#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>

#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

typedef struct MediaInput {
    AVFormatContext* formatCtx;
    AVCodecContext* codecCtx;
    AVCodec* codec;
    AVStream* stream;
}MediaInput_t;

typedef struct MediaOutput {

}MediaOutput_t;

enum MEDIATYPE {
    Media_Video,
    Media_Audio,
    Media_Subtittle,
    Media_ALL   // 音频视频和字幕
};

#endif // CLIPDEFINED_H
