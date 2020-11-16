#include "clip.h"

struct videoFilter {
    AVFilterContext* bufferCtx = nullptr;
    AVFilterGraph* graph = nullptr;
    AVFilterContext* sinkCtx = nullptr;
};

Clip::Clip(QObject *paren) : QObject(paren)
{

}

Clip::~Clip() {

}

bool Clip::init(MEDIATYPE type)
{

}

bool Clip::initVideoScale()
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

bool Clip::findBestStream()
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

/**
 * @brief Clip::initFilters
 * @param filterDescr， 当前主要用于视频中，音频里面如何使用filter呢？？
 *const char *filter_descr = "movie=my_logo.png[wm];[in][wm]overlay=5:5[out]";
 *需要叠加的水印为一张PNG（透明）图片（在这里是my_logo.png）。
 *const char *filters_descr = "lutyuv='u=128:v=128'";
 *const char *filters_descr = "hflip";
 *const char *filters_descr = "hue='h=60:s=-3'";
 *const char *filters_descr = "crop=2/3*in_w:2/3*in_h";
 *const char *filters_descr = "drawbox=x=200:y=200:w=300:h=300:color=pink@0.5";
 *const char *filters_descr = "movie=/storage/emulated/0/ws.jpg[wm];[in][wm]overlay=5:5[out]";
 *const char *filters_descr="drawgrid=width=100:height=100:thickness=4:color=pink@0.9";
 * @return
 */
bool Clip::initFilters(const QString& filterDescr)
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


bool Clip::initOutput()
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

bool Clip::initAudioResample()
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

bool Clip::addOutStream(MEDIATYPE type)
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

bool Clip::initAudioFifo()
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

AVFrame* Clip::allocVideoFrame(int width, int height, AVPixelFormat fmt)
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

AVFrame* Clip::allocAudioFrame(int channels, int sampleRate, AVSampleFormat sampleFmt, int nbSamples)
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
