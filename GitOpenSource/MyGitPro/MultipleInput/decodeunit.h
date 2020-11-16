#ifndef DECODEUNIT_H
#define DECODEUNIT_H

extern "C"
{
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavdevice/avdevice.h"

#include "libavutil/avutil.h"
#include "libavutil/avassert.h"
#include "libavutil/error.h"

#include "libavutil/imgutils.h"
#include "libswscale/swscale.h"

#include "libavfilter/avfilter.h"
#include "libavfilter/buffersrc.h"
#include "libavfilter/buffersink.h"
}


#include <QDebug>
#include <QMutex>
#include <QString>
#include <QFutureWatcher>
#include <QtConcurrent>

#define VIDEO_FRAME_RATE 30

struct FilterContext
{
    AVFilterContext* sink = nullptr;
    AVFilterGraph* graph = nullptr;
};

//解码之后将音视频数据写入同一个mp4文件中
class DecodeUnit
{
public:
    DecodeUnit();
    ~DecodeUnit(void);

public:

    void run();

    void SetParam(const char* formatName, const char* deviceName);

private:

    //打开输入文件
    int open_input();
    //打开输出
    int open_output();
    //
    void videoDecode();
    //
    int init_video_scale();

    int initVideoFilters(AVCodecContext* decCtx, AVCodecContext* encCtx);

    void videoEncode();

    int addOutStream();

    const QString averrorqstring(int errcode);

    void SaveYUV(AVFrame* fgrame,  char* filename);

private:

    QFutureWatcher<void> m_audio_watcher;
    //输入相关参数
    QString formatName = "";
    QString deviceName = "";

    AVFormatContext* fmt_ctx = NULL;
    AVCodecContext* dec_ctx;
    AVCodec* codec;
    AVStream* videoStream;
    int videoStreamIndex = 0;

    FilterContext m_video_filter;
    AVFilterContext* buffersrc_ctx;

    uint64_t videoPts =0;


    AVFrame* outAvframe = nullptr;
    //输出相关参数
    AVFormatContext* outfmt_ctx = nullptr; //输出文件的上下文
    AVDictionary *options = nullptr;

    AVCodecContext* enc_ctx;
    AVStream* outStream;
    AVCodec* codec_enc;

    int m_videoFrameIndex = 0;
    int64_t next_pts = 0;

    bool m_bIsRun = nullptr;
    SwsContext* m_swsContext;

};

#endif // DECODEUNIT_H
