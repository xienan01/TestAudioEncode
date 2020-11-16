#include "ffmpeghandle.h"
#include <QDateTime>
#include <QScreen>

#if defined(Q_OS_MAC)
#elif defined(Q_OS_WIN)
#include <Windows.h>
#include <dshow.h>
#endif

#define GETVALUE(key)  QString("%1").arg(key).toUtf8().constData()
#define PARSE_PARAM(name) m_params[name].toString().toUtf8().constData()

constexpr int MAX_FRAME_SIZE = 4000;
constexpr int MAX_LIST_SIZE = 30;
constexpr int MAX_FIFO_SIZE = 30;
constexpr int FLUSH_NUM = 2;
constexpr int TIMEOUT = 1000;

const QMap<QString, QString> VideoQuality = {
    {"high", "23.000"},
    {"standard", "28.000"},
    {"low", "41.000"},
};

namespace ffmpeghandle {

struct FilterContext
{
    AVFilterContext* bufferCtx = nullptr;
    AVFilterGraph* graph = nullptr;
    AVFilterContext* sinkCtx = nullptr;
};

struct StreamInfo
{
    AVFormatContext* fmtCtx = nullptr;
    AVCodecContext* codecContext = nullptr;
    AVStream* streams = nullptr;
};

FFmpegHandle::FFmpegHandle(QObject *parent) : QObject(parent), m_videoListFree(MAX_LIST_SIZE)
{

}

FFmpegHandle::~FFmpegHandle()
{

}

bool FFmpegHandle::initInput(const QVariantMap &params)
{
    int ret = 0;
    m_width = params["area"].toRect().width();
    m_height = params["area"].toRect().height();
    m_offsetX = params["area"].toRect().x();
    m_offsetY = params["area"].toRect().y();
    m_frameRate = params["frameRate"].toInt();

    if (m_width >= MAX_FRAME_SIZE || m_height >= MAX_FRAME_SIZE) {
        return false;
    }

    AVDictionary *opts = nullptr;
    int screenIndex = 0;
    QString videoInputName = QString("%1").arg(screenIndex);
#if defined(Q_OS_MAC)
    char videoFormatName[] = "avfoundation";
#elif defined(Q_OS_WIN)
    char videoFormatName[] = "gdigrab";
    videoInputName = "desktop";
#elif defined(Q_OS_LINUX)
    char videoFormatName[] = "x11grab";
    videoInputName = QString("%1%2%3%4").arg("0.0+").arg(m_offsetX).arg(",").arg(m_offsetY);
#endif
    m_streams[VideoInput] = QSharedPointer<StreamInfo>(new StreamInfo);
    AVInputFormat* input = av_find_input_format(videoFormatName);
    if (!input) {
        return false;
    }
    m_streams[VideoInput]->fmtCtx = avformat_alloc_context();
    QMap<QString, QString> items = {
        {"pixel_format", GETVALUE(m_format)},
        {"framerate", GETVALUE(m_frameRate)},
        {"offset_x", GETVALUE(m_offsetX)},
        {"offset_y", GETVALUE(m_offsetY)},
        {"video_size",QString("%1x%2").arg(m_width).arg(m_height).toUtf8().constData() }
    };
    foreach (auto key, items.keys()) {
        if (key.compare("framerate") == 0) {
            if (input && input->priv_class && av_opt_find(&input->priv_class, key.toUtf8().data(), NULL, 0, AV_OPT_SEARCH_FAKE_OBJ)) {

            }
        }
        av_dict_set(&opts, key.toUtf8().data(), items[key].toUtf8().data(), 0);
    }

    if ((ret = avformat_open_input(&m_streams[VideoInput]->fmtCtx,
                                   videoInputName.toUtf8().constData(),
                                   input,
                                   &opts)) < 0) {
        return false;
    }

    if (m_mute) {
        QString audioDeviceName = "";
#if defined(Q_OS_MAC)
        char audioFormatName[] = "avfoundation";
        audioDeviceName = params["audioDeviceName"].toString();
#elif defined(Q_OS_WIN)
        char audioFormatName[] = "dshow";
        audioDeviceName = "audio=" + GetMicrophoneDeviceName();
#elif defined(Q_OS_LINUX)
        char audioFormatName[] = "pulse";
        audioDeviceName = "default"
        #endif
                m_streams[AudioInput] = QSharedPointer<StreamInfo>(new StreamInfo);
        AVInputFormat* input = av_find_input_format(audioFormatName);
        m_streams[AudioInput]->fmtCtx = avformat_alloc_context();
        QMap<QString, QString> items = {
            {"sample_rate", QString("%1").arg(SAMPLE_RATE).toUtf8().constData()},
            {"channels", QString("%1").arg(SAMPLE_CHANNELS).toUtf8().constData()},
        };
        foreach (auto key, items.keys()) {
            if (input && input->priv_class && av_opt_find(&input->priv_class, key.toUtf8().data(), NULL, 0, AV_OPT_SEARCH_FAKE_OBJ)) {

            }
            av_dict_set_int(&opts, key.toUtf8().data(), items[key].toInt(), 0);
        }

        if ((avformat_open_input(&m_streams[AudioInput]->fmtCtx,
                                 audioDeviceName.toUtf8().constData(),
                                 input,
                                 &opts)) < 0) {
            return false;
        }
    }
    return true;
}

bool FFmpegHandle::findBestStream()
{
    auto findStream = [this] (StreamType type) -> bool {
        if (avformat_find_stream_info(m_streams[type]->fmtCtx, nullptr) < 0) {
            return false;
        } else {
            int streamIndex = -1;
            if (type == VideoInput) {
                streamIndex = av_find_best_stream(m_streams[type]->fmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
            } else if (m_mute) {
                streamIndex = av_find_best_stream(m_streams[type]->fmtCtx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
            }
            if (streamIndex < 0) {
                return false;
            } else {
                m_streams[type]->streams = m_streams[type]->fmtCtx->streams[streamIndex];

                AVCodec *decoder = avcodec_find_decoder(m_streams[type]->streams->codecpar->codec_id);
                if (decoder) {
                    m_streams[type]->codecContext = avcodec_alloc_context3(decoder);
                    avcodec_parameters_to_context(m_streams[type]->codecContext,
                                                  m_streams[type]->streams->codecpar);
                    if (avcodec_open2(m_streams[type]->codecContext, decoder, nullptr) < 0) {
                        return false;
                    }
                } else {
                    return false;
                }
                return true;
            }
        }
    };

    if (!findStream(VideoInput)) {
        return false;
    }

    if (m_mute) {
        if (!findStream(AudioInput)) {
            return false;
        }
    }
    return true;
}

bool FFmpegHandle::initVideoFilters(const QString& filterDescr)
{
    int ret = 0;
    const AVFilter *buffersrc  = avfilter_get_by_name("buffer");
    const AVFilter *buffersink = avfilter_get_by_name("buffersink");
    AVFilterInOut  *outputs = avfilter_inout_alloc();
    AVFilterInOut  *inputs  = avfilter_inout_alloc();

    AVCodecContext* codecCtx = m_streams[VideoInput]->codecContext;
    if (!codecCtx) {
        return false;
    }
    AVRational timebase = {1, m_frameRate};
    QString args = QString("video_size=%1x%2:pix_fmt=%3:time_base=%4/%5")
            .arg(codecCtx->width).arg(codecCtx->height).arg(codecCtx->pix_fmt)
            .arg(timebase.num).arg(timebase.den);
    m_videoFilter->graph = avfilter_graph_alloc();
    if (!outputs || !inputs || !m_videoFilter->graph) {
        goto __FAILED;
    }

    ret = avfilter_graph_create_filter(&m_videoFilter->bufferCtx, buffersrc, "in", args.toUtf8().constData(), NULL, m_videoFilter->graph);
    if (ret < 0) {
        goto __FAILED;
    }

    ret = avfilter_graph_create_filter(&m_videoFilter->sinkCtx, buffersink, "out", NULL, NULL, m_videoFilter->graph);
    if (ret < 0) {
        goto __FAILED;
    }

    ret = av_opt_set_bin(m_videoFilter->sinkCtx, "pix_fmts", (const uint8_t*)&codecCtx->pix_fmt, sizeof(codecCtx->pix_fmt), AV_OPT_SEARCH_CHILDREN);
    if (ret < 0) {
        goto __FAILED;
    }

    outputs->name       = av_strdup("in");
    outputs->filter_ctx = m_videoFilter->bufferCtx;
    outputs->pad_idx    = 0;
    outputs->next       = NULL;

    inputs->name       = av_strdup("out");
    inputs->filter_ctx = m_videoFilter->sinkCtx;
    inputs->pad_idx    = 0;
    inputs->next       = NULL;

    if ((ret = avfilter_graph_parse_ptr(m_videoFilter->graph, filterDescr.toUtf8().constData(), &inputs, &outputs, NULL)) < 0) {
        goto __FAILED;
    }

    if ((ret = avfilter_graph_config(m_videoFilter->graph, NULL)) < 0) {
        goto __FAILED;
    }

__FAILED:
    if (inputs) {
        avfilter_inout_free(&inputs);
        inputs = nullptr;
    }
    if (outputs) {
        avfilter_inout_free(&outputs);
        outputs = nullptr;
    }
    return true;
}

bool FFmpegHandle::initVideoScale()
{
    AVCodecContext* decCtx = m_streams[VideoInput]->codecContext;
    if (decCtx == nullptr) {
        return false;
    }
    AVCodecContext* encCtx = m_streams[VideoOutput]->codecContext;
    if (encCtx == nullptr) {
        return false;
    }

    int filll_picture_size = av_image_get_buffer_size(encCtx->pix_fmt, encCtx->width, encCtx->height, 32);
    m_videoScaleOutbuf = (uint8_t *)av_malloc(filll_picture_size);
    if (!m_videoScaleOutbuf) {
        return false;
    }

    m_scaleFrame = allocVideoFrame(m_width, m_height, m_format);
    if (!m_scaleFrame) {
        return false;
    }
    int ret = av_image_fill_arrays(m_scaleFrame->data, m_scaleFrame->linesize, m_videoScaleOutbuf,
                                   encCtx->pix_fmt, encCtx->width, encCtx->height, 4);
    if (ret <= 0) {
        if (m_scaleFrame) {
            av_frame_free(&m_scaleFrame);
        }
        av_free(m_videoScaleOutbuf);
        return false;
    }

    m_swsContext = sws_getContext(m_width, m_height, decCtx->pix_fmt,
                                  m_width, m_height, encCtx->pix_fmt,
                                  SWS_BICUBIC, NULL, NULL, NULL);
    if (!m_swsContext) {
        if (m_scaleFrame) {
            av_frame_free(&m_scaleFrame);
        }
        av_free(m_videoScaleOutbuf);
        sws_freeContext(m_swsContext);
        ret = false;
    }
    return true;
}

AVFrame* FFmpegHandle::allocVideoFrame(int width, int height, AVPixelFormat fmt)
{
    AVFrame *frame = av_frame_alloc();
    if (!frame) {
        return nullptr;
    }
    frame->width = width;
    frame->height = height;
    frame->format = fmt;
    if (av_frame_get_buffer(frame, 0) < 0) {
        av_frame_free(&frame);
        return nullptr;
    }
    return frame;
}

void FFmpegHandle::setStatus(FFmpegHandle::RecordState status)
{
    QMutexLocker locker(& m_statusMutex);
    if (m_status == status) {
        return;
    }

    m_status = status;
    emit statusChanged(m_status);
}

void FFmpegHandle::decodeVideo()
{
    int ret = 0;
    AVFormatContext* fmtCtx = m_streams[VideoInput]->fmtCtx;
    if (fmtCtx == nullptr) {
        return;
    }
    AVCodecContext* decCtx = m_streams[VideoInput]->codecContext;
    if (decCtx == nullptr) {
        return;
    }
    AVPacket packet;
    av_init_packet(&packet);
    AVFrame *frame = allocVideoFrame(decCtx->width, decCtx->height, decCtx->pix_fmt);
    if (!frame) {
        return;
    }

    AVFrame *newFrame = allocVideoFrame(m_width, m_height, decCtx->pix_fmt);
    if (!newFrame) {
        return;
    }

    while (true) {
        if (status() == Stopped) {
            break;
        }
        if (status() == Paused) {
            m_statusCondition.wait(&m_statusMutex);
        }

        packet.data = NULL;
        packet.size = 0;
        ret = av_read_frame(fmtCtx, &packet);
        if (ret < 0) {
            av_packet_unref(&packet);
            break;
        }
        ret = avcodec_send_packet(decCtx, &packet);
        av_packet_unref(&packet);
        while (ret >= 0) {
            ret = avcodec_receive_frame(decCtx, frame);
            if (ret == AVERROR(EAGAIN)) {
                break;
            }

            if (!m_videoListFree.tryAcquire(1, TIMEOUT)) {
                continue;
            }
            if (av_buffersrc_add_frame(m_videoFilter->bufferCtx, frame) < 0) {
                break;
            }

            ret = av_buffersink_get_frame(m_videoFilter->sinkCtx, newFrame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            } else if (ret < 0) {
                break;
            }
            ret = sws_scale(m_swsContext, newFrame->data, newFrame->linesize, 0,
                            m_height, m_scaleFrame->data, m_scaleFrame->linesize);
            if (ret != m_height) {
                return;
            }
            av_frame_unref(newFrame);
            m_frames.push_back(m_scaleFrame);
            m_videoListUsed.release();
            av_packet_unref(&packet);
        }
    }

    flushVideoDecode();
    av_packet_unref(&packet);
    av_frame_free(&frame);
    av_frame_free(&newFrame);
    closeInput(VideoInput);
    m_videoThreadExit = true;
}

int FFmpegHandle::flushVideoDecode()
{
    int ret = -1;
    AVFormatContext* fmtCtx = m_streams[VideoInput]->fmtCtx;
    if (fmtCtx == nullptr) {
        return -1;
    }
    AVCodecContext* decCtx = m_streams[VideoInput]->codecContext;
    if (decCtx == nullptr) {
        return -1;
    }
    AVFrame *frame = allocVideoFrame(decCtx->width, decCtx->height, decCtx->pix_fmt);
    if (!frame) {
        return -1;
    }
    AVFrame *newFrame = allocVideoFrame(m_width, m_height, decCtx->pix_fmt);
    if (!frame) {
        return -1;
    }
    ret = avcodec_send_packet(decCtx, nullptr);
    if (ret != 0) {
        return -1;
    }
    while (ret >= 0) {
        if ((ret = avcodec_receive_frame(decCtx, frame)) < 0) {
            if (ret == AVERROR_EOF) {

            } else if (ret == AVERROR(EAGAIN)) {

            }
            break;
        }
        if (!m_videoListFree.tryAcquire(1, TIMEOUT)) {
            continue;
        }
        if (av_buffersrc_add_frame(m_videoFilter->bufferCtx, frame) < 0) {
            break;
        }

        ret = av_buffersink_get_frame(m_videoFilter->sinkCtx, newFrame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        } else if (ret < 0) {
            break;
        }
        ret = sws_scale(m_swsContext, newFrame->data, newFrame->linesize, 0,
                        m_height, m_scaleFrame->data, m_scaleFrame->linesize);
        if (ret != m_height) {
            return false;
        }
        av_frame_unref(newFrame);
        m_frames.push_back(m_scaleFrame);
        m_videoListUsed.release();
    }
    av_frame_free(&frame);
    av_frame_free(&newFrame);
    return 0;
}

void FFmpegHandle::releaseVideoMemory()
{
    if (m_videoFilter) {
        if (m_videoFilter->graph) {
            avfilter_graph_free(&m_videoFilter->graph);
            m_videoFilter->graph = nullptr;
        }
        if (m_videoFilter->sinkCtx) {
            m_videoFilter->sinkCtx = nullptr;
        }
        delete m_videoFilter;
        m_videoFilter = nullptr;
    }

    if (m_swsContext) {
        sws_freeContext(m_swsContext);
        m_swsContext = nullptr;
    }

    if (m_videoScaleOutbuf) {
        av_free(m_videoScaleOutbuf);
        m_videoScaleOutbuf = nullptr;
    }

    if (m_scaleFrame) {
        // av_free(m_scaleFrame);
        m_scaleFrame = nullptr;
    }
    m_frames.clear();
}

FFmpegHandle::RecordState FFmpegHandle::status()
{
    QMutexLocker locker(&m_statusMutex);
    return m_status;
}

const QString FFmpegHandle::errorInfo(int errcode)
{
    char errInfo[128];
    av_strerror(errcode, errInfo, 128);
    return QString(errInfo);
}

void FFmpegHandle::saveYUV(AVFrame *frame, const char *sPath)
{
    FILE* file = fopen(sPath, "ab+");
    int wh = frame->width * frame->height;
    fwrite(frame->data[0], 1, wh, file);
    fwrite(frame->data[1], 1, wh / 4, file);
    fwrite(frame->data[2], 1, wh / 4, file);
    fclose(file);
}

MediaHandle::MediaHandle()
{
    setStatus(Unknown);
}

MediaHandle::~MediaHandle()
{

}

bool MediaHandle::init(const QVariantMap &params)
{
    if (params.isEmpty()) {
        return false;
    }
    m_params = params;
    if (params["mute"].toBool() == true) {
        m_mute = true;
    } else if (params["mute"].toBool() == false) {
        m_mute = false;
    } else {
        return false;
    }
    setStatus(Initing);
    m_outputFile = PARSE_PARAM("outputFileName");

#if defined(Q_OS_MAC)
    if (m_mute) {
        if (!screenrecord::RecordUtil::requestAVCaptureDevicePermission(AVMEDIA_TYPE_AUDIO)) {
            return false;
        }
    }
#endif

    av_register_all();
    avdevice_register_all();
    avcodec_register_all();
    avfilter_register_all();

    if (!initInput(params)) {
        return false;
    }
    if (!findBestStream()) {
        return false;
    }
    if (!initOutput()) {
        return false;
    }

    m_videoFilter = new FilterContext();
    QString crop = QString("crop=%1:%2:%3:%4").arg(m_width).arg(m_height).arg(m_offsetX).arg(m_offsetY);
    if (!initVideoFilters(crop)) {
        return false;
    }
    if (!initVideoScale()) {
        return false;
    }

    if (m_mute) {
        if (!initAudioFifo()) {
            return false;
        }
        if (!initAudioResample()) {
            return false;
        }
    }

    if (m_streams[VideoOutput] == nullptr || m_streams[VideoOutput]->fmtCtx == nullptr) {
        return false;
    }
    if (avformat_write_header(m_streams[VideoOutput]->fmtCtx, nullptr) < 0) {
        return false;
    }
    setStatus(Ready);
    return true;
}

bool MediaHandle::initOutput()
{
    int ret = 0;
    m_streams[VideoOutput] = QSharedPointer<StreamInfo>(new StreamInfo);
    ret = avformat_alloc_output_context2(&m_streams[VideoOutput]->fmtCtx, NULL, NULL,
                                         m_outputFile.toUtf8().data());
    if (!m_streams[VideoOutput]->fmtCtx || ret < 0) {
        return false;
    }

    if (!addOutStream(VideoOutput)) {
        return false;
    }

    if (m_mute) {
        m_streams[AudioOutput] = QSharedPointer<StreamInfo>(new StreamInfo);
        if (!m_streams[VideoOutput]) {
            ret = avformat_alloc_output_context2(&m_streams[AudioOutput]->fmtCtx, NULL, NULL,
                                                 m_outputFile.toUtf8().data());
            if (!m_streams[AudioOutput]->fmtCtx || ret < 0) {
                return false;
            }
        } else {
            m_streams[AudioOutput]->fmtCtx = m_streams[VideoOutput]->fmtCtx;
        }
        if (!addOutStream(AudioOutput)) {
            return false;
        }
    }

    if (!(m_streams[VideoOutput]->fmtCtx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&m_streams[VideoOutput]->fmtCtx->pb, m_outputFile.toUtf8().constData(), AVIO_FLAG_WRITE);
        if (ret < 0 ) {
            avformat_free_context(m_streams[VideoOutput]->fmtCtx);
            return false;
        }
    }
    return true;
}

bool MediaHandle::initAudioResample()
{
    AVCodecContext* decCtx = m_streams[AudioInput]->codecContext;
    if (decCtx == nullptr) {
        return false;
    }
    AVCodecContext* encCtx = m_streams[AudioOutput]->codecContext;
    if (encCtx == nullptr) {
        return false;
    }

    if (!(m_swrContext = swr_alloc())) {
        return false;
    }

    decCtx->channel_layout = decCtx->channel_layout == 0 ? av_get_default_channel_layout(decCtx->channels) : decCtx->channel_layout;
    m_swrContext = swr_alloc_set_opts(m_swrContext,
                                      encCtx->channel_layout, encCtx->sample_fmt, encCtx->sample_rate,
                                      decCtx->channel_layout, decCtx->sample_fmt, decCtx->sample_rate,
                                      0, NULL);
    if (!m_swrContext) {
        return false;
    }
    if ((swr_init(m_swrContext)) < 0) {
        swr_free(&m_swrContext);
        return false;
    }
    return true;
}

bool MediaHandle::addOutStream(StreamType type)
{
    auto deleteOutStream = [=] (AVCodecContext* codeCtx) {
        if (codeCtx) {
            avcodec_close(codeCtx);
            avcodec_free_context(&codeCtx);
            codeCtx = nullptr;
        }
    };

    AVFormatContext* outFmtCtx = m_streams[type]->fmtCtx;
    if (outFmtCtx == nullptr) {
        return false;
    }

    AVCodec* enCodec = nullptr;
    switch (type) {
    case VideoOutput: {
        int out_frame_width = m_width;
        int out_frame_height = m_height;
        if ((out_frame_width % 2) != 0) {
            ++out_frame_width;
        }
        if ((out_frame_height % 2) != 0) {
            ++out_frame_height;
        }

        enCodec = avcodec_find_encoder(AV_CODEC_ID_H264);
        if (!enCodec) {
            return false;
        }

        m_streams[type]->codecContext  = avcodec_alloc_context3(enCodec);
        if (!m_streams[type]->codecContext) {
            return false;
        }

        m_streams[type]->codecContext->pix_fmt = m_format;
        m_streams[type]->codecContext->width = out_frame_width;
        m_streams[type]->codecContext->height = out_frame_height;
        m_streams[type]->codecContext->time_base = AVRational{1, m_frameRate};
        m_streams[type]->codecContext->framerate = AVRational{m_frameRate, 1};
        m_streams[type]->codecContext->gop_size = 30;
        m_streams[type]->codecContext->max_b_frames = 28;
        auto key = QString("%1").arg(PARSE_PARAM("quality")).toUtf8().constData();
        av_opt_set(m_streams[type]->codecContext->priv_data, "crf",
                   VideoQuality[key].toUtf8().constData(), 0);
    } break;

    case AudioOutput: {
        enCodec = avcodec_find_encoder(AV_CODEC_ID_AAC);
        if (!enCodec) {
            return false;
        }
        m_streams[type]->codecContext = avcodec_alloc_context3(enCodec);
        m_streams[type]->codecContext->codec_type = AVMEDIA_TYPE_AUDIO;
        m_streams[type]->codecContext->sample_fmt = m_sampleFmt;
        m_streams[type]->codecContext->sample_rate = m_sampleRate;
        m_streams[type]->codecContext->frame_size  = m_nbSamples > 0 ? m_nbSamples : SAMPLE_NBSAMPLES;
        m_streams[type]->codecContext->bit_rate = 0;
        m_streams[type]->codecContext->profile = FF_PROFILE_AAC_HE_V2;
        m_streams[type]->codecContext->channels = m_channels;
        m_streams[type]->codecContext->channel_layout = av_get_default_channel_layout( m_streams[type]->codecContext->channels);
    } break;

    default:
        break;
    }

    m_streams[type]->streams = avformat_new_stream(outFmtCtx, enCodec);
    if (!m_streams[type]->streams) {
        deleteOutStream(m_streams[type]->codecContext);
        return false;
    } else {
        m_streams[type]->streams->index = 0;
        ++m_currentStreanIndex;
    }
    m_streams[type]->streams->index = m_currentStreanIndex;

    if (avcodec_parameters_from_context(m_streams[type]->streams->codecpar, m_streams[type]->codecContext) < 0) {
        deleteOutStream(m_streams[type]->codecContext);
        return false;
    }

    AVDictionary* opts = nullptr;
    if (m_streams[type]->codecContext->codec_id == AV_CODEC_ID_H264) {
        av_dict_set(&opts, "preset", "slow", 0);
    }

    if (avcodec_open2(m_streams[type]->codecContext, enCodec, &opts) < 0) {
        deleteOutStream(m_streams[type]->codecContext);
        return false;
    }

    if (outFmtCtx->oformat->flags & AVFMT_GLOBALHEADER) {
        m_streams[type]->codecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    return true;
}

bool MediaHandle::initAudioFifo()
{
    AVCodecContext* decCtx = m_streams[AudioInput]->codecContext;
    if (decCtx == nullptr) {
        return false;
    }
    AVCodecContext* encCtx = m_streams[AudioOutput]->codecContext;
    if (encCtx == nullptr) {
        return false;
    }

    m_nbSamples = encCtx->frame_size;
    if (!m_nbSamples) {
        m_nbSamples = 1024;
    }

    m_maxDstNbSamples = av_rescale_rnd(m_nbSamples, encCtx->sample_rate, decCtx->sample_rate, AV_ROUND_UP);
    m_dstNbSamples = m_maxDstNbSamples;
    m_audioFifoQueue = av_audio_fifo_alloc(encCtx->sample_fmt, encCtx->channels, MAX_FIFO_SIZE * m_nbSamples);
    if (!m_audioFifoQueue) {
        return false;
    }
    av_audio_fifo_reset(m_audioFifoQueue);
    return true;
}

int MediaHandle::resampleAudio(const AVFrame* frame, AVFrame* newFrame)
{
    int ret = 0;
    AVCodecContext* decCtx = m_streams[AudioInput]->codecContext;
    if (decCtx == nullptr) {
        return -1;
    }
    AVCodecContext* encCtx = m_streams[AudioOutput]->codecContext;
    if (encCtx == nullptr) {
        return -1;
    }

    if (decCtx->sample_fmt != encCtx->sample_fmt
            || decCtx->sample_rate != encCtx->sample_rate
            || decCtx->channels != encCtx->channels
            || decCtx->channel_layout != encCtx->channel_layout) {
        m_dstNbSamples = av_rescale_rnd(swr_get_delay(m_swrContext, decCtx->sample_rate) +
                                        frame->nb_samples, encCtx->sample_rate,
                                        frame->sample_rate, AV_ROUND_UP);
        if (m_dstNbSamples != m_maxDstNbSamples) {
            av_freep(&newFrame->data[0]);
            if ((ret = av_samples_alloc(newFrame->data, NULL, encCtx->channels,
                                        m_dstNbSamples, encCtx->sample_fmt, 1) < 0)) {
                return -1;
            }
            m_maxDstNbSamples = m_dstNbSamples;
        }

        newFrame->nb_samples = m_dstNbSamples;
        ret = swr_convert(m_swrContext, newFrame->data, m_dstNbSamples, (const uint8_t **)frame->data, newFrame->nb_samples);
        if (ret <= 0) {
            return -1;
        }
    } else {
        if (av_frame_copy(newFrame, frame) < 0) {
            return -1;
        }
    }
    return 0;
}

void MediaHandle::decodeAudio()
{
    int ret = 0;
    AVFormatContext* fmtCtx = nullptr;
    AVCodecContext* decCtx = nullptr;
    if (!(getEncodeContex(&fmtCtx, &decCtx, AudioInput))) {
        return;
    }
    AVPacket packet;
    av_init_packet(&packet);
    packet.data = NULL;
    packet.size = 0;

    AVFrame* frame = allocAudioFrame(decCtx->channels, decCtx->sample_rate, decCtx->sample_fmt, decCtx->frame_size);
    if (!frame) {
        return;
    }
    AVFrame* newFrame = allocAudioFrame(m_channels, m_sampleRate, m_sampleFmt, m_nbSamples);
    if (!newFrame) {
        return;
    }

    while (true) {
        if (status() == Stopped) {
            break;
        }
        if (status() == Paused) {
            m_statusCondition.wait(&m_statusMutex);
        }

        ret = av_read_frame(fmtCtx, &packet);
        if (ret < 0) {
            av_packet_unref(&packet);
            break;
        }
        ret = avcodec_send_packet(decCtx, &packet);
        av_packet_unref(&packet);
        while (ret >= 0) {
            ret = avcodec_receive_frame(decCtx, frame);
            if (ret == AVERROR(EAGAIN)) {
                break;
            }
            m_audioMutex.lock();
            if (av_audio_fifo_space(m_audioFifoQueue) < newFrame->nb_samples) {
                m_audioFifoNotFull.wait(&m_audioMutex);
            }
            m_audioMutex.unlock();

            if (resampleAudio(frame, newFrame) < 0) {
                continue;
            }

            m_audioMutex.lock();
            if ((av_audio_fifo_write(m_audioFifoQueue, (void**)newFrame->data, newFrame->nb_samples)) < newFrame->nb_samples) {
                m_audioMutex.unlock();
                continue;
            }

            if (av_audio_fifo_size(m_audioFifoQueue) >= m_nbSamples) {
                m_audioFifoNotEmpty.wakeAll();
            }
            m_audioMutex.unlock();
            av_packet_unref(&packet);
        }
    }

    if (newFrame->nb_samples != m_nbSamples) {
        av_freep(&newFrame->data[0]);
    } else {
        av_frame_free(&newFrame);
    }
    av_frame_free(&frame);
    av_packet_unref(&packet);
    flushAudioDecode();
    closeInput(AudioInput);
    m_audioThreadExit = true;
}

void MediaHandle::flushAudioDecode()
{
    int ret = 0;
    AVFormatContext* fmtCtx = nullptr;
    AVCodecContext* decCtx = nullptr;
    if (!(getEncodeContex(&fmtCtx, &decCtx, AudioInput))) {
        return;
    }

    AVFrame* frame = allocAudioFrame(decCtx->channels, decCtx->sample_rate, decCtx->sample_fmt, decCtx->frame_size);
    if (!frame) {
        return;
    }
    AVFrame* newFrame = allocAudioFrame(m_channels, m_sampleRate, m_sampleFmt, m_nbSamples);
    if (!newFrame) {
        return;
    }

    ret = avcodec_send_packet(decCtx, nullptr);
    while (ret >= 0) {
        ret = avcodec_receive_frame(decCtx, frame);
        if (ret == AVERROR(EAGAIN)) {
            break;
        }
        if (resampleAudio(frame, newFrame) < 0) {
            continue;
        }

        m_audioMutex.lock();
        if (av_audio_fifo_space(m_audioFifoQueue) < newFrame->nb_samples) {
            m_audioFifoNotFull.wait(&m_audioMutex);
        }
        if ((av_audio_fifo_write(m_audioFifoQueue, (void**)newFrame->data, newFrame->nb_samples)) < 0) {
            break;
        }
        if (av_audio_fifo_size(m_audioFifoQueue) >= m_nbSamples) {
            m_audioFifoNotEmpty.wakeAll();
        }
        m_audioMutex.unlock();
    }

    av_frame_free(&frame);
    av_freep(&newFrame->data[0]);
}

void MediaHandle::writeEncodeFrame(AVFrame* frame, bool isVideo)
{
    StreamType streamType;
    if (isVideo) {
        streamType = VideoOutput;
    } else {
        streamType = AudioOutput;
    }
    AVFormatContext* outFmtCtx = nullptr;
    AVCodecContext* encCtx = nullptr;
    if (!(getEncodeContex(&outFmtCtx, &encCtx, streamType))) {
        return;
    }

    AVPacket pkt;
    av_init_packet(&pkt);
    pkt.data = NULL;
    pkt.size = 0;

    if (frame) {
        if (encCtx->codec_type == AVMEDIA_TYPE_AUDIO) {
            frame->pts = m_audioFrameIndex++ * encCtx->frame_size;
        } else if (encCtx->codec_type == AVMEDIA_TYPE_VIDEO) {
            frame->pts = m_videoFrameIndex++;
        }
    }
    int ret = avcodec_send_frame(encCtx, frame);
    while (ret >= 0) {
        ret = avcodec_receive_packet(encCtx, &pkt);
        if (ret < 0) {
            if (ret == AVERROR_EOF) {

            } else if (ret == AVERROR(EAGAIN)) {

            }
            av_packet_unref(&pkt);
            break;
        }

        pkt.stream_index = m_streams[streamType]->streams->index;
        if (encCtx->codec_type == AVMEDIA_TYPE_AUDIO) {
            pkt.pts = m_audioFrameIndex * encCtx->frame_size;
            pkt.dts = m_audioFrameIndex * encCtx->frame_size;
            pkt.duration = encCtx->frame_size;
        } else if (encCtx->codec_type == AVMEDIA_TYPE_VIDEO) {
            int index = m_streams[streamType]->streams->index;
            pkt.pts = av_rescale_q_rnd(pkt.pts, encCtx->time_base, outFmtCtx->streams[index]->time_base, (AVRounding)(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
            pkt.dts = av_rescale_q_rnd(pkt.dts, encCtx->time_base, outFmtCtx->streams[index]->time_base, (AVRounding)(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
            pkt.duration = m_streams[streamType]->fmtCtx->streams[index]->time_base.den / m_streams[streamType]->fmtCtx->streams[index]->time_base.num / encCtx->time_base.den;
        }

        if (av_interleaved_write_frame(outFmtCtx, &pkt) < 0) {

        }
        av_packet_unref(&pkt);
    }
    av_packet_unref(&pkt);
}

void MediaHandle::doVideoProcess()
{
    AVFormatContext* outfmtCtx = nullptr;
    AVCodecContext* encVideoCtx = nullptr;
    if (!(getEncodeContex(&outfmtCtx, &encVideoCtx, VideoOutput))) {
        return;
    }

    AVFrame* videoFrame = allocVideoFrame(encVideoCtx->width, encVideoCtx->height, encVideoCtx->pix_fmt);
    if (!videoFrame) {
        return;
    }

    while (true) {
        if (status() == Stopped) {
            if (m_videoListFree.available() >= MAX_LIST_SIZE) {
                if (m_videoThreadExit) {
                    break;
                }
            }
        }
        if (status() != Stopped) {
            if (!m_videoListUsed.tryAcquire(1, TIMEOUT)) {
                continue;
            }
        }
        if (m_frames.size() > 0) {
            videoFrame = m_frames.front();
            m_frames.pop_front();
        }
        m_videoListFree.release();

        writeEncodeFrame(videoFrame, true);
        checkTime();
    }

    flushEncode(true);
    closeEncoder(VideoOutput);
    if (videoFrame) {
        av_frame_free(&videoFrame);
        videoFrame = nullptr;
    }
    checkTime();
    releaseVideoMemory();
    m_videoEncodeThreadExit = true;
    checkTrailAndClose();
}

void MediaHandle::doAudioProcess()
{
    AVFormatContext* outfmtCtx = nullptr;
    AVCodecContext* encAudioCtx = nullptr;
    if (!(getEncodeContex(&outfmtCtx, &encAudioCtx, AudioOutput))) {
        return;
    }

    AVFrame* frame = allocAudioFrame(encAudioCtx->channels, encAudioCtx->sample_rate, encAudioCtx->sample_fmt, m_nbSamples);
    if (!frame) {
        return;
    }

    while (true) {
        if (status() == Stopped) {
            if (m_audioMutex.try_lock()) {
                m_audioFifoNotEmpty.wakeAll();
                if (av_audio_fifo_size(m_audioFifoQueue) < m_nbSamples) {
                    av_audio_fifo_reset(m_audioFifoQueue);
                    if (m_audioThreadExit) {
                        m_audioMutex.unlock();
                        break;
                    }
                }
                m_audioMutex.unlock();
            }
        }

        m_audioMutex.lock();
        if (status() != Stopped) {
            if (av_audio_fifo_size(m_audioFifoQueue) < m_nbSamples) {
                m_audioFifoNotEmpty.wait(&m_audioMutex);
            }
        }
        if (av_audio_fifo_read(m_audioFifoQueue, (void **)frame->data, frame->nb_samples) < frame->nb_samples) {
            m_audioMutex.unlock();
            continue;
        }

        writeEncodeFrame(frame, false);
        if (av_audio_fifo_space(m_audioFifoQueue) >= m_dstNbSamples) {
            m_audioFifoNotFull.wakeAll();
        }
        m_audioMutex.unlock();
    }

    flushEncode(false);
    closeEncoder(AudioOutput);
    if (frame) {
        av_frame_free(&frame);
        frame = nullptr;
    }
    releaseAudioMemory();
    m_audioEncodeThreadExit = true;
    checkTrailAndClose();
}

void MediaHandle::flushEncode(bool isVideo)
{
    int nFlush = FLUSH_NUM;
    AVFormatContext* outfmtCtx = nullptr;
    AVCodecContext* encCtx = nullptr;
    StreamType streamType;
    if (isVideo) {
        streamType = VideoOutput;
    } else {
        streamType = AudioOutput;
    }
    if (!(getEncodeContex(&outfmtCtx, &encCtx, streamType))) {
        return;
    }

    AVPacket pkt;
    av_init_packet(&pkt);
    pkt.data = NULL;
    pkt.size = 0;

    if ((avcodec_send_frame(encCtx, nullptr)) != 0) {
        return;
    }
    do {
        int ret = 0;
        if ((ret = avcodec_receive_packet(encCtx, &pkt)) < 0) {
            av_packet_unref(&pkt);
            if (ret == AVERROR(EAGAIN)) {
                ret = 1;
                continue;
            } else if (ret == AVERROR_EOF) {
                if (!(--nFlush)) {
                    break;
                }
                continue;
            }
        }
        pkt.stream_index = m_streams[streamType]->streams->index;
        av_packet_rescale_ts(&pkt, encCtx->time_base, outfmtCtx->streams[pkt.stream_index]->time_base);
        ret = av_interleaved_write_frame(outfmtCtx, &pkt);
    } while (true);
    av_packet_unref(&pkt);
}

bool MediaHandle::getEncodeContex(AVFormatContext** fmtCtx, AVCodecContext** codecContext, StreamType streamType)
{
    QMutexLocker lock(&m_contexMutex);
    *fmtCtx = m_streams[streamType]->fmtCtx;
    if (fmtCtx == nullptr) {
        return false;
    }

    *codecContext = m_streams[streamType]->codecContext;
    if (codecContext == nullptr) {
        return false;
    }
    return true;
}

void MediaHandle::savePCM2(AVFrame* frame,  char* filename)
{
    FILE *f = fopen(filename, "a+");
    fwrite(frame->data[0], 1, frame->linesize[0], f);
    fwrite(frame->data[1], 1, frame->linesize[1], f);
    fclose(f);
}

void MediaHandle::savePCM(AVFrame* frame,  char* filename)
{
    FILE *f = fopen(filename, "a+");
    int data_size = av_get_bytes_per_sample(m_sampleFmt);
    if (data_size < 0) {
        fprintf(stderr, "Failed to calculate data size\n");
        exit(1);
    }
    for (int i = 0; i < frame->nb_samples; i++) {
        for (int ch = 0; ch < m_channels; ch++) {
            fwrite(frame->data[ch] + data_size * i, 1, data_size, f);
        }
    }
    fclose(f);
}

void MediaHandle::startRecord()
{
    if (status() == Ready) {
        QtConcurrent::run([=]() { FFmpegHandle::decodeVideo(); });
        QtConcurrent::run([=]() { doVideoProcess(); });

        if (m_mute) {
            QtConcurrent::run([=]() { decodeAudio(); });
            QtConcurrent::run([=]() { doAudioProcess(); });
        }
        setStatus(Started);
        m_startTime = QDateTime::currentMSecsSinceEpoch();
    } else if (status() == Paused) {
        setStatus(Started);
        m_totalPausedTime += QDateTime::currentMSecsSinceEpoch() - m_pauseTime;
        m_statusCondition.wakeAll();
    }
}

void MediaHandle::pause()
{
    setStatus(Paused);
}

void MediaHandle::stop()
{
    RecordState state = status();
    setStatus(Stopped);
    if (state == Paused) {
        m_statusCondition.wakeAll();
    }

    if (status() == Stopped) {
        if (m_mute) {
            m_audioFifoNotEmpty.wakeAll();
            m_audioFifoNotFull.wakeAll();
        }
    }
}

void MediaHandle::closeEncoder(StreamType type)
{
    if (m_streams[type]->streams || m_streams[type]->codecContext) {
        avcodec_close(m_streams[type]->codecContext);
        avcodec_free_context(&m_streams[type]->codecContext);
        m_streams[type]->codecContext = nullptr;
        m_streams[type]->streams = nullptr;
        if (type == VideoOutput) {
            m_videoFrameIndex = 0;
        } else {
            m_audioFrameIndex = 0;
        }
    }
    m_currentStreanIndex = -1;
}

void MediaHandle::checkTrailAndClose()
{
    m_exitMutex.lock();
    if (!m_mute) {
        if (!m_videoEncodeThreadExit) {
            m_exitMutex.unlock();
            return;
        }
    } else {
        if (!m_videoEncodeThreadExit || !m_audioEncodeThreadExit) {
            m_exitMutex.unlock();
            return;
        }
    }
    m_exitMutex.unlock();

    if (m_streams[VideoOutput]) {
        if (m_streams[VideoOutput]->fmtCtx) {
            av_write_trailer(m_streams[VideoOutput]->fmtCtx);
            if (m_streams[VideoOutput]->fmtCtx && !(m_streams[VideoOutput]->fmtCtx->oformat->flags & AVFMT_NOFILE)) {
                avio_closep(&m_streams[VideoOutput]->fmtCtx->pb);
                avformat_free_context(m_streams[VideoOutput]->fmtCtx);
            }
            m_streams[VideoOutput]->fmtCtx = nullptr;
            m_streams[VideoOutput]->streams = nullptr;
        }
        m_streams[VideoOutput] = nullptr;
        m_streams[AudioOutput] = nullptr;
    }

    emit finished(true);
}

void MediaHandle::checkTime()
{
    emit durationChanged(QDateTime::currentMSecsSinceEpoch() - m_startTime - m_totalPausedTime);
}

AVFrame* MediaHandle::allocAudioFrame(int channels, int sampleRate, AVSampleFormat sampleFmt, int nbSamples)
{
    AVFrame *frame = av_frame_alloc();
    if (!frame) {
        return nullptr;
    }
    frame->format = sampleFmt;
    int64_t channel_layout = av_get_default_channel_layout(channels);
    frame->channel_layout = channel_layout ? channel_layout : AV_CH_LAYOUT_STEREO;
    frame->sample_rate = sampleRate;
    frame->channels = channels;
    frame->nb_samples = nbSamples;
    if (nbSamples) {
        if (av_frame_get_buffer(frame, 0) < 0) {
            av_frame_free(&frame);
            return nullptr;
        }
    }
    return frame;
}

void FFmpegHandle::closeInput(StreamType type)
{
    if (m_streams[type]) {
        if (m_streams[type]->streams && m_streams[type]->codecContext) {
            avcodec_close(m_streams[type]->codecContext);
            avcodec_free_context(&m_streams[type]->codecContext);
            m_streams[type]->codecContext = nullptr;
            m_streams[type]->streams = nullptr;
        }
        if (m_streams[type]->fmtCtx) {
            avformat_close_input(&m_streams[type]->fmtCtx);
            avformat_free_context(m_streams[type]->fmtCtx);
            m_streams[type]->fmtCtx = nullptr;
        }
        m_streams[type] = nullptr;
    }
}

void MediaHandle::releaseAudioMemory()
{
    if (m_swrContext) {
        swr_free(&m_swrContext);
        m_swrContext = nullptr;
    }

    if (m_audioFifoQueue) {
        av_audio_fifo_free(m_audioFifoQueue);
        m_audioFifoQueue = nullptr;
    }
    m_audioFrameIndex = 0;
}

GifHandle::GifHandle()
{

}

GifHandle::~GifHandle()
{

}

bool GifHandle::init(const QVariantMap& params)
{
    return true;
}

void GifHandle::startRecord()
{
    if (status() == Ready) {
        setStatus(Started);
        if (!m_videoFuture.isRunning()) {
            m_videoFuture = QtConcurrent::run([=]() {
                FFmpegHandle::decodeVideo();
            });
        }
    } else if (status() == Paused) {
        setStatus(Started);
        m_statusCondition.wakeAll();
    }
}

void GifHandle::pause()
{
    setStatus(Paused);
}

void GifHandle::stop()
{
    RecordState state = status();
    setStatus(Stopped);
    if (state == Paused) {
        m_statusCondition.wakeAll();
    }
}
}
