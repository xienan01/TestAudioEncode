#ifndef FFMPEGHEADER_H
#define FFMPEGHEADER_H


extern "C"
{
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavdevice/avdevice.h"

#include "libavfilter/avfilter.h"
#include "libavfilter/buffersrc.h"
#include "libavfilter/buffersink.h"

#include "libavutil/opt.h"
#include "libavutil/common.h"
#include "libavutil/channel_layout.h"
#include "libavutil/imgutils.h"

#include "libavutil/avutil.h"
#include "libavutil/channel_layout.h"
#include "libavutil/mathematics.h"
#include "libavutil/timestamp.h"
#include "libavutil/avassert.h"
#include "libavutil/error.h"
#include "libavutil/time.h"

#include "libavutil/audio_fifo.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"

#include "SDL2/SDL.h"

}
#endif
