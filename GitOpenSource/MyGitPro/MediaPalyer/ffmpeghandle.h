#ifndef FFNPEGHANDLE_H
#define FFNPEGHANDLE_H

#include "ffmpegheader.h"
#include <QFuture>
#include <QVariantMap>
#include <QtConcurrent>
#include <QSharedPointer>

#define FRAME_RATE 25

#define SAMPLE_CHANNELS 2
#define SAMPLE_RATE 44100
#define SAMPLE_NBSAMPLES 1024

namespace ffmpeghandle {

struct FilterContext;
struct StreamInfo;

class FFmpegHandle : public QObject
{
    Q_OBJECT
public:
    explicit FFmpegHandle(QObject* parent = nullptr);
    virtual ~FFmpegHandle();

protected:
    enum StreamType {
        AudioInput,
        AudioOutput,
        VideoInput,
        VideoOutput
    };

    enum RecordState {
        Initing,
        Ready,
        Started,
        Paused,
        Stopped,
        Unknown,
    };

public:
    virtual bool init(const QVariantMap &params) = 0;
    virtual void startRecord() = 0;
    virtual void stop() = 0;
    virtual void pause() = 0;
    virtual void seek() = 0;


public:
    AVFrame* allocVideoFrame(int width, int height, AVPixelFormat fmt);
    RecordState status();

signals:
    void statusChanged(RecordState status);
    void durationChanged(qint64 duration);
    void finished(bool isFinished);

public slots:
    void setStatus(RecordState status);


protected:
    bool initInput(const QVariantMap &params);
    bool findBestStream();
    bool initVideoScale();
    bool initVideoFilters(const QString& filterDescr);
    void decodeVideo();
    int flushVideoDecode();
    void closeInput(StreamType type);
    void releaseVideoMemory();
    const QString errorInfo(int errcode);

protected:
    void saveYUV(AVFrame* frame, const char* sPath);

protected:
    QMap<StreamType, QSharedPointer<StreamInfo>> m_streams;
    QVariantMap m_params;
    QString m_outputFile = "";
    FilterContext* m_videoFilter = nullptr;
    QList<AVFrame*> m_frames;

    bool m_mute = true;
    int m_width = 0;
    int m_height = 0;
    int m_offsetX = 0;
    int m_offsetY = 0;
    int m_frameRate = FRAME_RATE;
    AVPixelFormat m_format = AV_PIX_FMT_YUV420P;

    QMutex m_statusMutex;
    QWaitCondition m_statusCondition;
    QSemaphore m_videoListFree, m_videoListUsed;

    bool m_videoThreadExit = false;

private:
    uint8_t* m_videoScaleOutbuf = nullptr;
    SwsContext* m_swsContext = nullptr;
    AVFrame* m_scaleFrame = nullptr;
    RecordState m_status = Unknown;
};

class MediaHandle : public FFmpegHandle
{
public:
    MediaHandle();
    virtual ~MediaHandle();

public:
    bool init(const QVariantMap& params) override;
    void startRecord() override;
    void pause() override;
    void stop() override;

private:
    QString GetMicrophoneDeviceName();
    bool initOutput();
    bool addOutStream(StreamType type);

    bool initAudioResample();
    bool initAudioFifo();
    void decodeAudio();
    int  resampleAudio(const AVFrame* frame, AVFrame* newFrame);
    void flushAudioDecode();
    void writeEncodeFrame(AVFrame* frame, bool isVideo);
    void flushEncode(bool isVideo);
    void closeEncoder(StreamType type);
    void doVideoProcess();
    void doAudioProcess();
    void releaseAudioMemory();
    void checkTrailAndClose();
    void checkTime();
    AVFrame* allocAudioFrame(int channels, int sampleRate, AVSampleFormat sampleFmt, int nbSamples);
    bool getEncodeContex(AVFormatContext** fmtCtx, AVCodecContext** codecContext, StreamType streamType);

private:
    void savePCM2(AVFrame* frame,  char* filename);
    void savePCM(AVFrame* frame,  char* filename);

private:
    int m_channels = SAMPLE_CHANNELS;
    int m_sampleRate = SAMPLE_RATE;
    int m_nbSamples = SAMPLE_NBSAMPLES;
    AVSampleFormat m_sampleFmt = AV_SAMPLE_FMT_FLTP;

    int m_dstNbSamples = 0;
    int m_maxDstNbSamples = 0;
    SwrContext* m_swrContext =nullptr;
    AVAudioFifo* m_audioFifoQueue = nullptr;

    uint64_t m_audioFrameIndex = 0;
    uint64_t m_videoFrameIndex = 0;

    int m_currentStreanIndex = -1;

    QMutex m_audioMutex;
    QWaitCondition m_audioFifoNotFull, m_audioFifoNotEmpty;

    QMutex m_contexMutex;
    QMutex m_exitMutex;

    bool m_audioThreadExit = false;
    bool m_videoEncodeThreadExit = false;
    bool m_audioEncodeThreadExit = false;

    qint64 m_startTime = 0;
    qint64 m_pauseTime = 0;
    qint64 m_totalPausedTime = 0;
};
}

#endif
