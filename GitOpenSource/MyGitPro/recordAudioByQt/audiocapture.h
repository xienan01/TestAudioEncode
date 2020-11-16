#ifndef AUDIOCAPTURE_H
#define AUDIOCAPTURE_H

#include <QMap>
#include <QList>
#include <QObject>
#include <QAudioInput>
#include <QAudioFormat>
#include <QAudioOutput>

class AudioCapture : QObject
{
    Q_OBJECT

public:
    explicit AudioCapture(QObject* parent = nullptr);

 public:
    Q_INVOKABLE bool init();
    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();

public:
    // 采样大小
    enum SAMPLESIZE {
        SAMPLE_SIZE_8 = 8,
        SAMPLE_SIZE_16 = 16
    };

public:
    void setVolume(qreal volume);
    qreal getVolume();
    // 获得所有可用的设备
    QMap<int, QString> getAllDevice();
    // 设置用于录音的设置索引，索引由getAllDevice()方法返回，设置非法的索引将会使用默认录入设备
    void setDeviceByIndex(int);
    QList<int> GetAllSampleRates();
    QList<int> GetAllSampleChannels();
    // 已经录入数据大小
    size_t bytesReady();
    size_t read(char* buf, size_t len);
    // 设置一帧的大小
    void setFrameSize(int size);
    void setChannelNum(int channels);
    void setSampleRate(int sample_rates);
    void setSampleSize(SAMPLESIZE sample_size);
    void setSampleType(QAudioFormat::SampleType sample_type);

    int getFrameSize() { return m_frameSize; }
    int getChannelNum() { return m_audioFmt.channelCount(); }
    int getSampleRate() { return m_audioFmt.sampleRate(); }
    int getSampleSize() { return m_audioFmt.sampleSize(); }
    int getSampleType() { return m_audioFmt.sampleType(); }


private:
    int m_frameSize = 1024;
    int m_channels = 2;
    int m_sampleRate = 44100;
    int m_sampleBits = 16;
    QAudioInput* m_recorder = nullptr;
    QIODevice* m_ioDevice = nullptr;
    QAudioFormat m_audioFmt;

    QAudioDeviceInfo m_currDevice;
    QList<QAudioDeviceInfo> m_deviceLists; //音频录入设备列表
    QMap<int, QString> m_deviceMap;

    QList<int> sample_support_rate_;
    QList<int> sample_support_channels_;

signals:
    void frameFull();
};

#endif // AUDIOCAPTURE_H
