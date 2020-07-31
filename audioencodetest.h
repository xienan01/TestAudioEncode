#ifndef AUDIOENCODETEST_H
#define AUDIOENCODETEST_H

extern "C"
{
#include <libavcodec/avcodec.h>

#include <libavutil/channel_layout.h>
#include <libavutil/common.h>
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>


#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

}


#include <QDebug>
#include <QMutex>
#include <QString>

class audioEncodeTest
{
public:
    audioEncodeTest();

    void Test();

private:
     int check_sample_fmt(const AVCodec *codec, enum AVSampleFormat sample_fmt);

     int select_sample_rate(const AVCodec *codec);

     int select_channel_layout(const AVCodec *codec);

private:

};

#endif // AUDIOENCODETEST_H
