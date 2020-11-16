#ifndef DECODEUNIT_H
#define DECODEUNIT_H

extern "C"
{
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavdevice/avdevice.h"

#include "libavfilter/avfilter.h"
#include "libavfilter/buffersrc.h"
#include "libavfilter/buffersink.h"

// libav resample
#include "libavutil/opt.h"
#include "libavutil/common.h"
#include "libavutil/channel_layout.h"
#include "libavutil/imgutils.h"

#include "libavutil/avutil.h"
#include "libavutil/channel_layout.h"
#include "libavutil/opt.h"
#include "libavutil/mathematics.h"
#include "libavutil/timestamp.h"
#include "libavutil/avassert.h"
#include "libavutil/error.h"
#include "libavutil/time.h"

#include "libavutil/audio_fifo.h"

#include "libswscale/swscale.h"
#include "libswresample/swresample.h"

}


#include <QDebug>
#include <QMutex>
#include <QString>
#include <QFutureWatcher>
#include <QtConcurrent>


typedef struct audioInfo {
    AVSampleFormat out_Sample_fmt;
    int outSample_channels;
    int outSample_rate;
}audioInfo_i;
//解码之后将音视频数据写入同一个mp4文件中
class DecodeUnit
{
public:
    DecodeUnit();
    ~DecodeUnit(void);

public:

    void run();

    void SetParam(const audioInfo_i* param, const char* formatName, const char* deviceName);

    int  stop();

private:

    //打开输入设备，采集音频数据
    int open_input();
    //打开输出
    int open_output();
    //
    void audioDecode();
    //初始化audio fifo
    int init_audio_fifo();
    //初始化重采样
    int initAudioResample(AVCodecContext* encCtx, AVCodecContext* decCtx);
    //重采样
    int resampleAudio(AVCodecContext* decCtx, AVCodecContext* encCtx, AVFrame* frame, AVFrame* newFrame);
    //重新编码
    int do_audioReEncode();

    void audioEncode(AVCodecContext* encCtx, AVFrame* frame);

    void adts_header(char *szAdtsHeader, int dataLen, int aactype, int frequency, int channels);

    int addOutStream();

    const QString averrorqstring(int errcode);

    void SavePCM(AVFrame* fgrame,  char* filename);

    void spcm(AVFrame* frame,  char* filename);

    int get_audio_obj_type(int aactype);

    int get_sample_rate_index(int freq, int aactype);

    int get_channel_config(int channels, int aactype);

private:

    QFutureWatcher<void> m_audio_watcher;
    //输入相关参数
    QString formatName = "";
    QString deviceName = "";

    AVFormatContext* fmt_ctx = nullptr;
    AVCodecContext* dec_ctx;
    AVCodec* codec;
    AVStream* audioStream;
    int audioStreamIndex = 0;
    AVAudioFifo*  m_fifoAudioQueue = nullptr;
    uint64_t audioPts =0;


    //重采样
    SwrContext* m_swsAudioContext;
    int  dstNbSamples = 0, maxDstNbSamples = 0;
    //输出相关参数
    AVFormatContext* outfmt_ctx = nullptr; //输出文件的上下文
    AVCodecContext* enc_ctx;
    AVStream* outStream;
    AVCodec* codec_enc;

    int m_nbSamples = 0 ;

    int m_audioFrameIndex = 0;
    int64_t next_pts = 0;

    //fifo交互数据
    uint8_t **src_data = nullptr;
    uint8_t **dst_data = nullptr;
    int src_linesize;
    int dst_linesize;

    QMutex m_audioMutex;


    bool m_bIsRun;

};

#endif // DECODEUNIT_H
