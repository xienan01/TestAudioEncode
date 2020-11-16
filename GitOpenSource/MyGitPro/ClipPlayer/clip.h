#ifndef CLIP_H
#define CLIP_H

/**
 * 视频文件裁剪 播放
 * Media clip or paly
 * 解楠 Xie Nan
 * 本程序是一个最简单的视频裁剪和播放渲染
 *支持文件：
 *支持视频、音频、字幕的裁剪(音视频同步处理)
 */

#include "clipdefined.h"
#include <QObject>

struct videoFilter;
class Clip : public QObject
{
    Q_OBJECT

public:
    explicit Clip(QObject* paren = nullptr);
    virtual ~Clip();

public:

    struct videoParam {
        int x;
        int y;
        int width;
        int height;
        AVPixelFormat format = AV_PIX_FMT_YUV420P;
    };

    struct audioParam {
        int sampleChannels;
        int sampleLayout;
        int sampleRate;
        AVSampleFormat smple_format = AV_SAMPLE_FMT_FLTP;
    };

private:
    //all
    bool init(MEDIATYPE type);
    bool initVideoScale();
    bool findBestStream();
    bool addOutStream(MEDIATYPE type);

    //video
    bool initFilters(const QString& filterDescr);

    //audio
    bool initAudioResample();
    bool initAudioFifo();
    void releaseVideoMemory();

    AVFrame* allocVideoFrame(int width, int height, AVPixelFormat fmt);
    AVFrame* allocAudioFrame(int channels, int sampleRate, AVSampleFormat sampleFmt, int nbSamples);

    //for test
private:
    void savePCM2(AVFrame* frame, char* filename);
    void savePCM(AVFrame* frame, char* filename);
    const QString errorInfo(int errcode);

private:
    videoFilter* m_videoFilter = nullptr;
    SwsContext* m_swsContext = nullptr;
    SwrContext* m_swrContext = nullptr;
    AVAudioFifo *m_audioFifoQueue = nullptr;
    int m_maxDstNbSamples = 0;
    int m_dstNbSamples = 0;
    int m_nbSamples = 0;

    MEDIATYPE m_type = Media_ALL;
};

#endif // CLIP_H
