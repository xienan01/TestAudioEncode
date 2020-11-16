#include <QCoreApplication>
#include <iostream>

#ifdef __cplusplus
extern "C" {
#include "libavformat/avformat.h"
#include "libavfilter/avfilter.h"
#include "libavutil/opt.h"
#include "libavfilter/buffersrc.h"
#include "libavfilter/buffersink.h"
#include "libswscale/swscale.h"
#include "libavutil/imgutils.h"
}
#endif

#define INPUT_PATH "/Users/xienan/Desktop/4.mov"
#define OUTPUT_PATH "/Users/xienan/Test/test.gif"

const char *filter_descr = "format=pix_fmts=rgb32,fps=10,scale=320:240:flags=lanczos,split [o1] [o2];[o1] palettegen [p]; [o2] fifo [o3];[o3] [p] paletteuse";

static AVFormatContext* ofmt_ctx;
static AVCodecContext* o_codec_ctx;

static AVFilterGraph* filter_graph;
static AVFilterContext* buffersrc_ctx;
static AVFilterContext* buffersink_ctx;

static int init_filters(const char* filter_desc,
                        AVFormatContext* ifmt_ctx,
                        int stream_index,
                        AVCodecContext* dec_ctx)
{
    char args[512];
    int ret = 0;
    AVFilter* buffersrc = avfilter_get_by_name("buffer");
    AVFilter* buffersink = avfilter_get_by_name("buffersink");
    AVFilterInOut* inputs = avfilter_inout_alloc();
    AVFilterInOut* outputs = avfilter_inout_alloc();
    AVRational time_base = ifmt_ctx->streams[stream_index]->time_base;

    enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_PAL8, AV_PIX_FMT_NONE };

    filter_graph = avfilter_graph_alloc();
    if (!outputs || !inputs || !filter_graph) {
        ret = AVERROR(ENOMEM);
        return ret;
    }

    snprintf(args, sizeof(args),
             "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
             dec_ctx->width, dec_ctx->height, dec_ctx->pix_fmt,
             time_base.num, time_base.den,
             dec_ctx->sample_aspect_ratio.num, dec_ctx->sample_aspect_ratio.den);

    ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in", args, nullptr, filter_graph);
    if (ret < 0) {
        av_log(nullptr, AV_LOG_ERROR, "Cannot create buffer source\n");
        return ret;
    }

    ret = avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out", nullptr, nullptr, filter_graph);
    if (ret < 0) {
        av_log(nullptr, AV_LOG_ERROR, "Cannot create buffer sink\n");
        return ret;
    }

    av_opt_set_int_list(buffersink_ctx, "pix_fmts", pix_fmts, AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
    if (ret < 0) {
        av_log(nullptr, AV_LOG_ERROR, "can not set output pixel format\n");
        return ret;
    }

    outputs->name = av_strdup("in");
    outputs->filter_ctx = buffersrc_ctx;
    outputs->pad_idx = 0;
    outputs->next = nullptr;

    inputs->name = av_strdup("out");
    inputs->filter_ctx = buffersink_ctx;
    inputs->pad_idx = 0;
    inputs->next = nullptr;

    if ((ret = avfilter_graph_parse_ptr(filter_graph, filter_desc, &inputs, &outputs, nullptr)) < 0) {
        av_log(nullptr, AV_LOG_ERROR, "parse filter graph error\n");
        return ret;
    }

    if ((ret = avfilter_graph_config(filter_graph, nullptr)) < 0) {
        av_log(nullptr, AV_LOG_ERROR, "config graph error\n");
    }

    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);

    return 0;
}

static int init_muxer()
{
    int ret = 0;
    AVOutputFormat* o_fmt = av_guess_format("gif", OUTPUT_PATH, "video/gif");
    ret = avformat_alloc_output_context2(&ofmt_ctx, o_fmt, "gif", OUTPUT_PATH);
    if (ret < 0) {
        av_log(nullptr, AV_LOG_ERROR, "%s allocate output format\n", av_err2str(ret));
        return -1;
    }

    AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_GIF);
    if (!codec) {
        return -1;
    }

    AVStream* stream = avformat_new_stream(ofmt_ctx, codec);

    AVCodecParameters* codec_paramters = stream->codecpar;
    codec_paramters->codec_tag = 0;
    codec_paramters->codec_id = codec->id;
    codec_paramters->codec_type = AVMEDIA_TYPE_VIDEO;
    codec_paramters->width = 320;
    codec_paramters->height = 240;
    codec_paramters->format = AV_PIX_FMT_PAL8;

    o_codec_ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(o_codec_ctx, codec_paramters);

    o_codec_ctx->time_base = { 1, 10 };

    if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
        o_codec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    ret = avcodec_open2(o_codec_ctx, codec, NULL);
    if (ret < 0) {
        av_log(nullptr, AV_LOG_ERROR, "%s open output codec\n", av_err2str(ret));
        return ret;
    }

    ret = avio_open(&ofmt_ctx->pb, OUTPUT_PATH, AVIO_FLAG_WRITE);
    if (ret < 0) {
        av_log(nullptr, AV_LOG_ERROR, "%s avio open error\n", av_err2str(ret));
        return ret;
    }

    ret = avformat_write_header(ofmt_ctx, NULL);
    if (ret < 0) {
        av_log(nullptr, AV_LOG_ERROR, "%s write header\n", av_err2str(ret));
        return ret;
    }

    av_dump_format(ofmt_ctx, -1, OUTPUT_PATH, 1);

    return 0;
}

static void destroy_muxer()
{
    avformat_free_context(ofmt_ctx);
    avcodec_close(o_codec_ctx);
    avcodec_free_context(&o_codec_ctx);
}

static void destroy_filter()
{
    avfilter_free(buffersrc_ctx);
    avfilter_free(buffersink_ctx);
    avfilter_graph_free(&filter_graph);
}

static void muxing_one_frame(AVFrame* frame)
{
    int ret = avcodec_send_frame(o_codec_ctx, frame);
    AVPacket *pkt = av_packet_alloc();
    av_init_packet(pkt);

    while (ret >= 0) {
        ret = avcodec_receive_packet(o_codec_ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        }

        av_write_frame(ofmt_ctx, pkt);
    }

    av_packet_unref(pkt);
}

static int process() {
    av_register_all();
    avcodec_register_all();
    avfilter_register_all();

    int ret = 0;
    AVFormatContext* fmt_ctx = avformat_alloc_context();
    AVCodecContext* codec_ctx = nullptr;
    AVCodec* codec = nullptr;
    int video_index = -1;

    AVPacket *pkt = av_packet_alloc();
    av_init_packet(pkt);
    pkt->size = 0;
    pkt->data = nullptr;

    ret = avformat_open_input(&fmt_ctx, INPUT_PATH, NULL, NULL);
    if (ret != 0) {
        std::cerr << "error open input" << std::endl;
        goto die;
    }

    avformat_find_stream_info(fmt_ctx, NULL);
    av_dump_format(fmt_ctx, -1, INPUT_PATH, 0);

    ret = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (ret == AVERROR_STREAM_NOT_FOUND) {
        std::cerr << "error no video stream found" << std::endl;
        goto die;
    }

    for (unsigned i = 0; i < fmt_ctx->nb_streams; i++) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_index = i;

            if (ret == AVERROR_DECODER_NOT_FOUND) {
                codec = avcodec_find_decoder(fmt_ctx->streams[i]->codecpar->codec_id);
            }

            break;
        }
    }

    if (!codec) {
        std::cerr << "could not find the decoder" << std::endl;
        goto die;
    }

    codec_ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codec_ctx, fmt_ctx->streams[video_index]->codecpar);
    ret = avcodec_open2(codec_ctx, codec, NULL);
    if (ret < 0) {
        std::cerr << "open codec error" << std::endl;
        goto die;
    }

    if (init_muxer() < 0) {
        av_log(nullptr, AV_LOG_ERROR, "could not init muxer\n");
        goto die;
    }

    if ((ret = init_filters(filter_descr, fmt_ctx, video_index, codec_ctx)) < 0) {
        av_log(nullptr, AV_LOG_ERROR, "could not init filters %s\n", av_err2str(ret));
        goto die;
    }

    // it's time to decode
    while (av_read_frame(fmt_ctx, pkt) == 0) {
        if (pkt->stream_index == video_index) {

            ret = avcodec_send_packet(codec_ctx, pkt);

            while (ret >= 0) {
                AVFrame* frame = av_frame_alloc();
                ret = avcodec_receive_frame(codec_ctx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                }

                if (ret >= 0) {
                    // processing one frame
                    frame->pts = frame->best_effort_timestamp;

                    // palettegen need a whole stream, just add frame to buffer and don't get frame
                    ret = av_buffersrc_add_frame_flags(buffersrc_ctx, frame, AV_BUFFERSRC_FLAG_KEEP_REF);
                    if (ret < 0) {
                        av_log(nullptr, AV_LOG_ERROR, "error add frame to buffer source %s\n", av_err2str(ret));
                    }
                }
                av_frame_free(&frame);
            }
        }
    }

    // end of buffer
    if ((ret = av_buffersrc_add_frame_flags(buffersrc_ctx, nullptr, AV_BUFFERSRC_FLAG_KEEP_REF)) >= 0) {
        do {
            AVFrame* filter_frame = av_frame_alloc();
            ret = av_buffersink_get_frame(buffersink_ctx, filter_frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                // av_log(nullptr, AV_LOG_ERROR, "error get frame from buffer sink %s\n", av_err2str(ret));
                av_frame_unref(filter_frame);
                break;
            }

            // write the filter frame to output file
            muxing_one_frame(filter_frame);
            av_log(nullptr, AV_LOG_INFO, "muxing one frame\n");

            av_frame_unref(filter_frame);
        } while (ret >= 0);
    } else {
        av_log(nullptr, AV_LOG_ERROR, "error add frame to buffer source %s\n", av_err2str(ret));
    }

    av_packet_free(&pkt);

    av_write_trailer(ofmt_ctx);
    destroy_muxer();
    destroy_filter();
die:
    avformat_free_context(fmt_ctx);
    avcodec_close(codec_ctx);
    avcodec_free_context(&codec_ctx);

    return 0;
}


int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);
    process();
    return a.exec();
}








