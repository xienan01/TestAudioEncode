///视频filter

#include <QDebug>


#include "decodeunit.h"

DecodeUnit::DecodeUnit()
{
    av_register_all();
    avdevice_register_all();
    avfilter_register_all();
}

DecodeUnit::~DecodeUnit(void)
{

}

void DecodeUnit::run()
{
    int ret = 0;
    ret = open_input();
    if(ret != 0) {
        qDebug() << "can't open device ";
        return;
    }
    ret = open_output();
    if(ret != 0) {

    }

    ret = initVideoFilters(dec_ctx, enc_ctx);
    if(ret < 0) {
        qDebug() << "can't init video filter";
        return;
    }
    m_audio_watcher.setFuture(QtConcurrent::run(this, &DecodeUnit::videoDecode));

    videoEncode();
}

int DecodeUnit::open_input()
{
    int ret = 0;
    ret = avformat_open_input(&fmt_ctx, deviceName.toUtf8().constData(), NULL, NULL);
    if(ret < 0) {
        qDebug() << "avformat_open_input open failed";
        return -1;
    }

    if((ret = avformat_find_stream_info(fmt_ctx, nullptr)) < 0) {
        qDebug() << "could not find stream information " << averrorqstring(ret);
        return -1;
    }

    if((ret = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0)) < 0)
    {
        qDebug() << "cound not find audio stream " << averrorqstring(ret);
        return -1;
    }
    else {
        videoStream = fmt_ctx->streams[ret];
        videoStreamIndex = ret;
    }

    codec = avcodec_find_decoder(fmt_ctx->streams[ret]->codecpar->codec_id);
    if(!codec) {
        qDebug() << "could not open decoder codec " << averrorqstring(ret);
        return -1;
    }
    dec_ctx = avcodec_alloc_context3(codec);
    if(!dec_ctx) {
        qDebug() << "could not alloc decoder decoder_ctx " << averrorqstring(ret);
        return -1;
    }
    avcodec_parameters_to_context(dec_ctx, fmt_ctx->streams[ret]->codecpar);
    if((ret = avcodec_open2(dec_ctx, codec, NULL)) < 0) {
        qDebug() << "could not open decoder codec " << averrorqstring(ret);
    }
    return ret;
}

int DecodeUnit::open_output()
{
    int ret = -1;
    QString m_output_file_name = QString("%1/Screen_Record_%2.mp4")
            .arg("/Users/xienan/Test")
            .arg(QDateTime::currentDateTime().toString("yyyy_mm_dd_hh_mm_ss"));
    ret = avformat_alloc_output_context2(&outfmt_ctx, NULL, NULL, m_output_file_name.toUtf8().constData());
    if(ret < 0 || !outfmt_ctx) {
        return -1;
    }

    if(addOutStream() != 0)
    {
        qDebug() << "add stream failed!";
        return -1;
    }
    av_dump_format(outfmt_ctx, 0, m_output_file_name.toUtf8().constData(), 1);
    if (!(outfmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&outfmt_ctx->pb, m_output_file_name.toUtf8().constData(), AVIO_FLAG_WRITE);
        if(ret < 0 ) {
            qDebug() << "can't open output file\n";
            avformat_free_context(outfmt_ctx);
            return -1;
        }
    }
    ret = avformat_write_header(outfmt_ctx, nullptr);
    if(ret < 0 ) {
        qDebug() <<  "--------xn------- avformat_write_header failed: " << averrorqstring(ret);
        avio_closep(&outfmt_ctx->pb);
        avformat_free_context(outfmt_ctx);
        return -1;
    }

    return 0;
}


int DecodeUnit::addOutStream()
{
    int ret = 0;
    // New stream
    outStream = avformat_new_stream(outfmt_ctx, NULL);
    if(!codec_enc) {
        qDebug() << "could not new video stream,error " << averrorqstring(ret);
        return -1;
    } else {
        outStream->index = 0;
    }

    codec_enc = avcodec_find_encoder(AV_CODEC_ID_H264);
    if(!codec_enc) {
        qDebug() << "could not find encoder,error ";
        return -1;
    }

    enc_ctx = avcodec_alloc_context3(codec_enc);
    if(! enc_ctx) {
        qDebug() << "could not alloc codec_ctx,error " << averrorqstring(ret);
        return -1;
    }

    videoPts = 0;
    enc_ctx->bit_rate = dec_ctx->width  * dec_ctx->height * 1.5 * VIDEO_FRAME_RATE;
    enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    enc_ctx->width = dec_ctx->width;
    enc_ctx->height = dec_ctx->height;
    enc_ctx->sample_aspect_ratio =  dec_ctx->sample_aspect_ratio;
    enc_ctx->time_base = AVRational{1, VIDEO_FRAME_RATE};
    enc_ctx->framerate = AVRational{VIDEO_FRAME_RATE, 1};
    enc_ctx->gop_size = 30;
    enc_ctx->max_b_frames = 28;
    ret = avcodec_parameters_from_context(outStream->codecpar,  enc_ctx);
    if(ret <0) {
        qDebug() << "could not copy parametrers from video codec " << averrorqstring(ret);
        return ret;
    }

    if( enc_ctx->codec_id == AV_CODEC_ID_H264) {
        av_dict_set(&options, "preset", "slow", 0);
    }

    if(init_video_scale() <= 0) {
        qDebug() << "initialize video swscontext failed!";
        return -1;
    }

    if(outfmt_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
        enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    ret = avcodec_open2(enc_ctx, codec_enc, NULL);
    if (ret < 0)  {
        qDebug() << "could not open audio codec " << averrorqstring(ret);
        avcodec_free_context(&enc_ctx);
        return ret;
    }

    return 0;
}


void DecodeUnit::videoDecode()
{
    int ret = -1;
    AVPacket packet;
    av_init_packet(&packet);
    AVFrame *frame = av_frame_alloc();
    while(!m_bIsRun) {
        packet.data = NULL;
        packet.size = 0;
        ret = av_read_frame(fmt_ctx, &packet);
        if (ret < 0) {
            qDebug() << "failed to read video frame " << averrorqstring(ret);
            av_packet_unref(&packet);
            break;
        }

        if(packet.stream_index == videoStreamIndex) {
            ret = avcodec_send_packet(dec_ctx, &packet);
            if (ret < 0) {
                qDebug() << "Error while sending a packet to the decoder\n";
                break;
            }
            av_packet_unref(&packet);

            while (ret >= 0) {
                ret = avcodec_receive_frame(dec_ctx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                } else if (ret < 0) {
                    break;
                }
                if(sws_scale(m_swsContext, frame->data, frame->linesize, 0, dec_ctx->height, outAvframe->data, outAvframe->linesize) < 0)
                {
                    qDebug() << "sws_scale frame failed\n";
                    break;
                }

                if (av_buffersrc_add_frame_flags(buffersrc_ctx, outAvframe, AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
                    qDebug() << "Error while feeding the filtergraph\n";
                    av_frame_free(&outAvframe);
                    break;
                }
            }
        }
    }

    av_frame_free(&frame);
    if(outAvframe) {
        av_frame_free(&outAvframe);
    }
    av_packet_unref(&packet);
    avformat_close_input(&fmt_ctx);
}

void DecodeUnit::videoEncode()
{
    int ret =0;
    AVFrame *frame = av_frame_alloc();
    frame->width = enc_ctx->width;
    frame->height = enc_ctx->height;
    frame->format = enc_ctx->pix_fmt;
    while(m_bIsRun) {
        ret = av_buffersink_get_frame(m_video_filter.sink, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            av_frame_free(&frame);
            return;
        }
        if (ret < 0) {
            qDebug() << "get frame filed"<< averrorqstring(ret);
            av_frame_free(&frame);
            return;
        }

        AVPacket pkt;
        av_init_packet(&pkt);
        pkt.data = NULL;
        pkt.size = 0;

        if(frame) {
            frame->pts = m_videoFrameIndex * ((1 / av_q2d(outStream->time_base)) / outStream->time_base.den);
        }

        ret = avcodec_send_frame(enc_ctx, frame);
        while(ret >= 0) {
            ret = avcodec_receive_packet(enc_ctx, &pkt);
            if(ret == AVERROR_EOF) {
                next_pts = 0x7fffffffffffffff;
                break;
            }
            else if (ret == AVERROR(EAGAIN)) {
                break;
            }

            if(ret < 0) {
                av_packet_unref(&pkt);
                qDebug() << "-----avcodec_receive_packet error! ";
                break;
            }

            pkt.pts = av_rescale_q_rnd(pkt.pts, enc_ctx->time_base,outStream->time_base, (AVRounding)(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
            pkt.dts = av_rescale_q_rnd(pkt.dts, enc_ctx->time_base,outStream->time_base, (AVRounding)(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
            pkt.duration =outStream->time_base.den /outStream->time_base.num / enc_ctx->time_base.den;
            ++m_videoFrameIndex;

            ret = av_interleaved_write_frame(outfmt_ctx, &pkt);
            if(ret < 0) {
                qDebug() << "------------- av_interleaved_write_frame failed" << averrorqstring(ret);
            }
            qDebug() << "----- writing  v number:" << m_videoFrameIndex << "frame ----" << next_pts;
            av_packet_unref(&pkt);
        }
    }
}


int DecodeUnit::init_video_scale()
{
    int nbyte = av_image_get_buffer_size(dec_ctx->pix_fmt,
                                         dec_ctx->width,
                                         dec_ctx->height, 32);
    uint8_t *video_outbuf = (uint8_t *)av_malloc(nbyte);
    if (!video_outbuf) {
        qDebug() << "avframe alloc failed.";
        return -1;
    }

    outAvframe  = av_frame_alloc();
    outAvframe->width = dec_ctx->width;
    outAvframe->height = dec_ctx->height;
    outAvframe->format  = dec_ctx->pix_fmt;
    int ret = av_image_fill_arrays(outAvframe->data, outAvframe->linesize, video_outbuf,
                                   dec_ctx->pix_fmt, dec_ctx->width, dec_ctx->height, 4);
    if (0 > ret) {
        qDebug() << "av frame fill arrays failed\n";
        if(outAvframe) {
            av_frame_free(&outAvframe);
        }
        av_free(video_outbuf);
        return -1;
    }

    m_swsContext = sws_getContext(dec_ctx->width, dec_ctx->height, dec_ctx->pix_fmt,
                                  dec_ctx->width, dec_ctx->height, enc_ctx->pix_fmt,
                                  SWS_BICUBIC, NULL, NULL, NULL);
    if(!m_swsContext) {
        qDebug() << "could not get scale context !";
        if(outAvframe) {
            av_frame_free(&outAvframe);
        }
        av_free(video_outbuf);
        ret = -1;
    }
    return ret;
}
int DecodeUnit::initVideoFilters(AVCodecContext* decCtx, AVCodecContext* enc_ctx)
{
    int ret = 0;
    const AVFilter *buffersrc  = avfilter_get_by_name("buffer");
    const AVFilter *buffersink = avfilter_get_by_name("buffersink");
    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs  = avfilter_inout_alloc();

    auto filter_free = [=] (AVFilterInOut *inputs, AVFilterInOut *outputs) {
        avfilter_inout_free(&inputs);
        avfilter_inout_free(&outputs);
    };

    m_video_filter.graph = avfilter_graph_alloc();
    if (!outputs || !inputs || !m_video_filter.graph) {
        qDebug() << "Cannot allocate memory " << averrorqstring(ret);
        filter_free(inputs, outputs);
        return -1;
    }

    enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    enc_ctx->time_base = AVRational{25,1};
    enc_ctx->sample_aspect_ratio = AVRational{16,9};
    QString args = QString("video_size=%1x%2:pix_fmt=%3:time_base=%4/%5:pixel_aspect=%6/%7")
            .arg(decCtx->width).arg(decCtx->height).arg(enc_ctx->pix_fmt)
            .arg(enc_ctx->time_base.num).arg(enc_ctx->time_base.den)
            .arg(enc_ctx->sample_aspect_ratio.den).arg(enc_ctx->sample_aspect_ratio.num);
    qDebug() <<  "---------xn ------- args:  "  << args ;
    ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in", args.toUtf8().constData(), NULL, m_video_filter.graph);
    if (ret < 0) {
        qDebug() << "could not create buffer source " << averrorqstring(ret);
        filter_free(inputs, outputs);
        return ret;
    }

    ret = avfilter_graph_create_filter(&m_video_filter.sink, buffersink, "out", NULL, NULL, m_video_filter.graph);
    if (ret < 0) {
        qDebug() << "could not create buffer sink " << averrorqstring(ret);
        filter_free(inputs, outputs);
        return ret;
    }

    ret = av_opt_set_bin(m_video_filter.sink, "pix_fmts", (const uint8_t*)&enc_ctx->pix_fmt, sizeof(enc_ctx->pix_fmt), AV_OPT_SEARCH_CHILDREN);
    if (ret < 0) {
        qDebug() << "could not set output pixel format " << averrorqstring(ret);
        filter_free(inputs, outputs);
        return ret;
    }

    outputs->name       = av_strdup("in");
    outputs->filter_ctx = buffersrc_ctx;
    outputs->pad_idx    = 0;
    outputs->next       = NULL;

    inputs->name       = av_strdup("out");
    inputs->filter_ctx = m_video_filter.sink;
    inputs->pad_idx    = 0;
    inputs->next       = NULL;

    //裁剪区域
    QString crop = QString("crop=%1:%2:%3:%4")
            .arg(enc_ctx->width).arg(enc_ctx->height).arg(200)
            .arg(100);
// crop ="-filter_complex"[0:v]pad=iw*3:ih[a];[a][1:v]overlay=w*[b];[b][2:v]overlay=w*2"

    if ((ret = avfilter_graph_parse_ptr(m_video_filter.graph, crop.toUtf8().constData(), &inputs, &outputs, NULL)) < 0) {
        filter_free(inputs, outputs);
        return ret;
    }

    ret = avfilter_link(buffersrc_ctx, 0, m_video_filter.sink, 0);


    if ((ret = avfilter_graph_config(m_video_filter.graph, NULL)) < 0) {
        filter_free(inputs, outputs);
        return ret;
    }

    filter_free(inputs, outputs);
    return ret;
}

void DecodeUnit::SetParam(const char* formatname, const char* devicename)
{
    formatName  = formatname;
    deviceName  = devicename;
}

void DecodeUnit::SaveYUV(AVFrame* frame,  char* filename)
{
    FILE *f = fopen(filename, "a+");

    //test audio
    int data_size = av_get_bytes_per_sample(dec_ctx->sample_fmt);
    if (data_size < 0) {
        fprintf(stderr, "Failed to calculate data size\n");
        exit(1);
    }
    for (int i = 0; i < frame->nb_samples; i++)
        for (int ch = 0; ch < dec_ctx->channels; ch++)
            fwrite(frame->data[ch] + data_size * i, 1, data_size, f);
    fclose(f);
}

const QString DecodeUnit::averrorqstring(int errcode)
{
    char errInfo[128];
    av_strerror(errcode, errInfo, 128);
    return QString(errInfo);
}
