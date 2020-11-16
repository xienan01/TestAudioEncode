#include "audiocapture.h"

AudioCapture::AudioCapture(QObject *parent) : QObject(parent)
{
    m_currDevice = QAudioDeviceInfo::defaultInputDevice();
}

bool AudioCapture::init()
{
    m_audioFmt.setCodec("audio/pcm");
    m_audioFmt.setSampleRate(m_sampleRate);
    m_audioFmt.setChannelCount(m_channels);
    m_audioFmt.setSampleSize(m_sampleBits);
    m_audioFmt.setByteOrder(QAudioFormat::LittleEndian);
    m_audioFmt.setSampleType(QAudioFormat::SignedInt);
    sample_support_rate_ = m_currDevice.supportedSampleRates();
    sample_support_channels_ = m_currDevice.supportedChannelCounts();
    m_deviceLists = QAudioDeviceInfo::availableDevices(QAudio::AudioInput);
    for (int i = 0; i < m_deviceLists.count(); i++) {
        QAudioDeviceInfo device = m_deviceLists.at(i);
        m_deviceMap[i] = device.deviceName();
    }
}

void AudioCapture::start()
{
    if ( m_currDevice.isNull()) {
        return;
    }
    if (!m_currDevice.isFormatSupported(m_audioFmt)) {
        //fmt =m_currDevice.nearestFormat(fmt);
        return;
    } else {
        m_recorder = new QAudioInput(m_audioFmt);
        //开始录制音频
        m_ioDevice = m_recorder->start();
    }
}

void AudioCapture::stop()
{
    if (m_recorder && m_ioDevice) {
        m_recorder->stop();
    }
}

void AudioCapture::setVolume(qreal volume)
{
    if (m_recorder) {
        m_recorder->setVolume(volume);
    }
}

qreal AudioCapture::getVolume()
{
    if (m_recorder) {
        return m_recorder->volume();
    } else {
        return 1.0;
    }
}

void AudioCapture::setDeviceByIndex(int idx)
{
    if (idx < 0 || idx >=  m_deviceMap.size()) {
        m_currDevice = QAudioDeviceInfo::defaultInputDevice();
    } else {
        m_currDevice = m_deviceLists.at(idx);
    }
    sample_support_rate_ =m_currDevice.supportedSampleRates();
    sample_support_channels_ =m_currDevice.supportedChannelCounts();
}

QList<int> AudioCapture::GetAllSampleRates()
{
    return sample_support_rate_;
}

QList<int> AudioCapture::GetAllSampleChannels()
{
    return sample_support_channels_;
}

QMap<int, QString> AudioCapture::getAllDevice()
{
    return  m_deviceMap;
}

size_t AudioCapture::bytesReady()
{
    if (m_recorder) {
        return m_recorder->bytesReady();
    }
    return 0;
}

size_t AudioCapture::read(char* buf, size_t len)
{
    if (m_recorder && m_ioDevice) {
        return m_ioDevice->read(buf, len);
    }
    return 0;
}

void AudioCapture::setFrameSize(int size)
{
    m_frameSize = size;
}

void AudioCapture::setChannelNum(int channels)
{
    m_audioFmt.setChannelCount(channels);
}

void AudioCapture::setSampleRate(int sample_rates)
{
    m_audioFmt.setSampleRate(sample_rates);
}

void AudioCapture::setSampleSize(SAMPLESIZE sample_size)
{
    m_audioFmt.setSampleSize(sample_size);
}

void AudioCapture::setSampleType(QAudioFormat::SampleType sample_type)
{
    m_audioFmt.setSampleType(sample_type);
}
