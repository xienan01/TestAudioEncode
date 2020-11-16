///音频采集-->解码-->重采样->编码

#include <QDebug>


#include "decodeunit.h"

DecodeUnit::DecodeUnit()
{
    av_register_all();    
    avdevice_register_all();
    m_bIsRun = false;
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

    ret =  open_output();
    if(ret != 0) {

    }

    ret = initAudioResample(enc_ctx, dec_ctx);
    if(ret != 0) {
        qDebug() << "can't init audio resample";
        return;
    }

    ret = init_audio_fifo();
    if(ret < 0) {
        qDebug() << "can't init audio fifo";
        return;
    }

    m_audio_watcher.setFuture(QtConcurrent::run(this, &DecodeUnit::audioDecode));

    do_audioReEncode();
}

int DecodeUnit::open_input()
{
    int ret = 0;
    //deviceName.toUtf8().constData()
    AVInputFormat *iformat = av_find_input_format("avfoundation");
    ret = avformat_open_input(&fmt_ctx, ":0", iformat, NULL);
    if(ret < 0) {
        qDebug() << "avformat_open_input open failed" << averrorqstring(ret);
        return -1;
    }

    if((ret = avformat_find_stream_info(fmt_ctx, nullptr)) < 0) {
        qDebug() << "could not find stream information " << averrorqstring(ret);
        return -1;
    }

    if((ret = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0)) < 0)
    {
        qDebug() << "cound not find audio stream " << averrorqstring(ret);
        return -1;
    }
    else {
        audioStream = fmt_ctx->streams[ret];
        audioStreamIndex = ret;
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

int DecodeUnit::initAudioResample(AVCodecContext* encCtx, AVCodecContext* decCtx)
{
    int ret = 0;
    m_swsAudioContext = swr_alloc();
    if (!m_swsAudioContext)
    {
        return -1;
    }

    m_swsAudioContext = swr_alloc_set_opts(m_swsAudioContext,
                                           encCtx->channel_layout,
                                           encCtx->sample_fmt,
                                           encCtx->sample_rate,
                                           decCtx->channel_layout,
                                           decCtx->sample_fmt,
                                           decCtx->sample_rate,
                                           0,
                                           NULL);


    if ((ret = swr_init(m_swsAudioContext)) < 0)
    {
        swr_free(&m_swsAudioContext);
        qDebug()  << "init swr failed!" << averrorqstring(ret);
        return -1;
    }
    return 0;
}


int DecodeUnit::resampleAudio(AVCodecContext* decCtx, AVCodecContext* encCtx, AVFrame* frame, AVFrame* newFrame)
{
    int ret = 0;
    if(!decCtx || !encCtx) {
        return -1;
    }

    if(decCtx->sample_fmt != encCtx->sample_fmt
            || decCtx->sample_rate != encCtx->sample_rate
            || decCtx->channels != encCtx->channels
            || decCtx->channel_layout != encCtx->channel_layout)
    {
        dstNbSamples = av_rescale_rnd(swr_get_delay(m_swsAudioContext, decCtx->sample_rate) +
                                      frame->nb_samples, encCtx->sample_rate,
                                      frame->sample_rate, AV_ROUND_UP);
        if (dstNbSamples > maxDstNbSamples) {
            av_freep(&dst_data[0]);
            av_freep(&dst_data);
            if((ret = av_samples_alloc(dst_data, NULL, encCtx->channels,
                                       dstNbSamples, encCtx->sample_fmt, 1) < 0))
            {
                return -1;
            }
            maxDstNbSamples = dstNbSamples;
        }

        newFrame->nb_samples = dstNbSamples;
        ret = swr_convert(m_swsAudioContext, dst_data, dstNbSamples, (const uint8_t **)frame->data, frame->nb_samples);
        if(ret <= 0)
        {
            return -1;
        }

        int dst_buf_size = dstNbSamples * av_get_bytes_per_sample(encCtx->sample_fmt);
        for (int k = 0; k < enc_ctx->channels; ++k) {
            for (int j = 0; j < dst_buf_size; ++j) {
                newFrame->data[k][j] = dst_data[k][j];
            }
        }
    } else {
        if(av_frame_copy(newFrame, frame) < 0)
        {
            return -1;
        }
    }
    return 0;
}

int DecodeUnit::stop()
{
    if(m_swsAudioContext) {
        swr_free(&m_swsAudioContext);
        m_swsAudioContext = nullptr;
    }
      m_bIsRun = true;
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
    int ret = -1;
    outStream = avformat_new_stream(outfmt_ctx,  NULL);
    if(!outStream) {
        qDebug() << "could not new audio stream,error " << averrorqstring(ret);
        return -1;
    }

    codec_enc = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if(!codec_enc) {
        qDebug() << "could not find audio encoder,error " << averrorqstring(ret);
        return -1;
    }
    codec_enc->type = AVMEDIA_TYPE_AUDIO;
    enc_ctx = avcodec_alloc_context3(codec_enc);
    enc_ctx->sample_fmt = AV_SAMPLE_FMT_FLTP;
    enc_ctx->sample_rate = dec_ctx->sample_rate;

    enc_ctx->frame_size  = dec_ctx->frame_size > 0 ? dec_ctx->frame_size: 1024;
    enc_ctx->time_base = AVRational{1, enc_ctx->sample_rate};

    enc_ctx->channels = dec_ctx->channels;
    enc_ctx->channel_layout = av_get_default_channel_layout(enc_ctx->channels);
    outStream->index = 0;

    outStream->time_base = AVRational{1, enc_ctx->sample_rate};
    ret = avcodec_parameters_from_context(outStream->codecpar, enc_ctx);
    if(ret <0) {
        qDebug() << "could not copy parametrers from audio codec " << averrorqstring(ret);
        avcodec_free_context(&enc_ctx);
        return ret;
    }

    ret = avcodec_open2(enc_ctx, codec_enc, NULL);
    if (ret < 0)  {
        qDebug() << "could not open audio codec " << averrorqstring(ret);
        avcodec_free_context(&enc_ctx);
        return ret;
    }

    if(outfmt_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
        enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    return 0;
}


void DecodeUnit::audioDecode()
{
    qDebug() << "decode thread  ---------";
    int ret = -1;
    AVPacket packet;
    av_init_packet(&packet);
    AVFrame *frame = av_frame_alloc();
    frame->format = dec_ctx->sample_fmt;
    frame->channel_layout = dec_ctx->channel_layout;
    frame->sample_rate = dec_ctx->sample_rate;
    frame->nb_samples = dec_ctx->frame_size > 0 ? dec_ctx->frame_size : 1024;
    ret = av_frame_get_buffer(frame, 0);
    if (ret < 0)
    {
        qDebug() << "av_frame_get_buffer failed" << averrorqstring(ret);
        return;
    }

    AVFrame *newFrame = av_frame_alloc();
    newFrame->format = enc_ctx->sample_fmt;
    newFrame->channel_layout = enc_ctx->channel_layout;
    newFrame->sample_rate = enc_ctx->sample_rate;
    newFrame->nb_samples = enc_ctx->frame_size > 0 ? enc_ctx->frame_size : 1024;
    ret = av_frame_get_buffer(newFrame, 0);
    if (ret < 0)
    {
        qDebug() << "alloc newFrame failed" << averrorqstring(ret);
        return;
    }

    packet.data = NULL;
    packet.size = 0;

    while(!m_bIsRun)
    {
        ret = av_read_frame(fmt_ctx, &packet);
        if (ret < 0) {
            qDebug() << "failed to read audio frame " << averrorqstring(ret);
            av_packet_unref(&packet);
            break;
        }

        qDebug() << "decode frame OK " << ret;
        if(packet.stream_index == audioStream->index) {
            ret = avcodec_send_packet(dec_ctx, &packet);
            while(ret >= 0) {
                ret = avcodec_receive_frame(dec_ctx, frame);
                if(ret < 0 ){
                     qDebug() << "recive  frame failed!" << ret;
                     break;
                } else {
                    qDebug() << "recive  frame OK!" << ret;
                }

                //audio resample
                if(0 != av_frame_make_writable(newFrame))
                {
                    qDebug() << "newFrame wrong!";
                    break;
                }

                if(resampleAudio(dec_ctx, enc_ctx, frame, newFrame) < 0)
                {
                    //qDebug() << "audio resample failed";
                    continue;
                }

                //SavePCM(newFrame, "/Users/xienan/Test/write.pcm");
                FILE *f = fopen("/Users/xienan/Test/write.pcm", "a+");
                fwrite(frame->data[0], 1, frame->linesize[0], f);
                fclose(f);


                av_packet_unref(&packet);
                {
                    QMutexLocker locker(&m_audioMutex);
                    qDebug() << "frame->nb_samples:  " << frame->nb_samples;
                    if(av_audio_fifo_space(m_fifoAudioQueue) <= frame->nb_samples) {
                        ret = av_audio_fifo_realloc(m_fifoAudioQueue, av_audio_fifo_size(m_fifoAudioQueue) + frame->nb_samples);
                        if(ret < 0) {
                            qDebug() << "audio fifo realloc  failed!\n";
                            break;
                        }
                    }

                    ret = av_audio_fifo_write(m_fifoAudioQueue, (void**)newFrame->data, newFrame->nb_samples);
                    if(ret < 0) {
                        qDebug() <<  "add audio rame to  queue failed!" << averrorqstring(ret);
                    } else {
                        qDebug() <<  "add audio rame to  queue OK!";
                    }
                }
            }
            continue;
        }
    }

    m_bIsRun = true;
    av_packet_unref(&packet);
    av_frame_free(&frame);
    av_frame_free(&newFrame);
    avformat_close_input(&fmt_ctx);
    return;
}

void DecodeUnit::audioEncode(AVCodecContext* encCtx, AVFrame* frame)
{
    AVPacket pkt;
    av_init_packet(&pkt);
    pkt.data = NULL;
    pkt.size = 0;

    if(frame) {
        frame->pts = m_audioFrameIndex * encCtx->frame_size;
    }

    int ret = 0;

    FILE* file = fopen("/Users/xienan/Test/pkt.aac", "ab+");
    ret = avcodec_send_frame(encCtx, frame);
    int aac_type = enc_ctx->profile;
    while(ret >= 0) {
        ret = avcodec_receive_packet(encCtx, &pkt);
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


        char adts_header_buf[7];
        adts_header(adts_header_buf, pkt.size, aac_type, 44100, 2);
        fwrite(adts_header_buf, 1, 7, file);
        fwrite(pkt.data, 1, pkt.size, file);

        pkt.stream_index = outStream->index;
        pkt.pts = m_audioFrameIndex * encCtx->frame_size;
        pkt.dts = m_audioFrameIndex * encCtx->frame_size;
        pkt.duration = encCtx->frame_size;
        ++m_audioFrameIndex;
        next_pts = pkt.pts;

        ret = av_interleaved_write_frame(outfmt_ctx, &pkt);
        if(ret < 0) {
            qDebug() << "------------- av_interleaved_write_frame failed" << averrorqstring(ret);
        }

        qDebug() << "----- writing  a number:" << m_audioFrameIndex << "frame ----" << next_pts;
        av_packet_unref(&pkt);

    }
    fclose(file);
}



int DecodeUnit::init_audio_fifo()
{
    int ret = 0;
    m_nbSamples = dec_ctx->frame_size;
    if (!m_nbSamples)
    {
        m_nbSamples = 1024;
    }

    maxDstNbSamples = av_rescale_rnd(m_nbSamples, enc_ctx->sample_rate, dec_ctx->sample_rate, AV_ROUND_UP);
    dstNbSamples = maxDstNbSamples;

    if(av_samples_alloc_array_and_samples(&dst_data, &dst_linesize, dec_ctx->channels, 1024,
                                          dec_ctx->sample_fmt, 0) < 0)
    {
        qDebug() << "alloc output cache failed";
        return -1;
    }

    //alloc fifo  缓存30帧
    m_fifoAudioQueue = av_audio_fifo_alloc(dec_ctx->sample_fmt, dec_ctx->channels, 30 *  m_nbSamples);
    if (!m_fifoAudioQueue)
    {
        qDebug() << "av_audio_fifo_alloc failed";
        ret = -1;
    }
    return ret;
}

int DecodeUnit::do_audioReEncode()
{
    qDebug() << "encode thread    -------------";
    int ret = 0;
    if (NULL == m_fifoAudioQueue)
    {
        qDebug() << "m_fifoAudioQueue is  null -------------";
        return -1;
    }

    AVFrame *frame = av_frame_alloc();
    frame->nb_samples = 1024; //dec_ctx->frame_size;
    frame->channels = enc_ctx->channels;
    frame->channel_layout = av_get_default_channel_layout(frame->channels);
    frame->format = enc_ctx->sample_fmt;
    frame->sample_rate = enc_ctx->sample_rate;
    av_frame_get_buffer(frame, 0);

    qDebug() << "encode  thread status:  -------------" << m_bIsRun;
    while(!m_bIsRun) {
        int currentAudioSize = av_audio_fifo_size(m_fifoAudioQueue);
        if(currentAudioSize >= (dec_ctx->frame_size > 0 ? enc_ctx->frame_size : 1024)) {
            QMutexLocker locker(&m_audioMutex);
            ret = av_audio_fifo_read(m_fifoAudioQueue, (void **)frame->data,  (enc_ctx->frame_size > 0 ? enc_ctx->frame_size : 1024));
            if(ret == frame->nb_samples) {
                //编码写入文件
                //SavePCM(frame, "/Users/xienan/Test/read.pcm");
                audioEncode(enc_ctx, frame);
            }
        }
    }

    av_write_trailer(outfmt_ctx);
    av_frame_free(&frame);
    return ret;
}

void DecodeUnit::SetParam(const audioInfo_i* param, const char* formatname, const char* devicename)
{
    formatName  = formatname;
    deviceName  = devicename;
}

void DecodeUnit::SavePCM(AVFrame* frame,  char* filename)
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

void DecodeUnit::spcm(AVFrame* frame,  char* filename)
{
    FILE *f = fopen(filename, "a+");
    fwrite(frame->data[0] , 1, frame->linesize[0], f);
    fclose(f);
}

const QString DecodeUnit::averrorqstring(int errcode)
{
    char errInfo[128];
    av_strerror(errcode, errInfo, 128);
    return QString(errInfo);
}


#define ADTS_HEADER_LEN  7;


void DecodeUnit::adts_header(char *szAdtsHeader, int dataLen, int aactype, int frequency, int channels) {

    int audio_object_type = get_audio_obj_type(aactype);
    int sampling_frequency_index = get_sample_rate_index(frequency, aactype);
    int channel_config = get_channel_config(channels, aactype);

    printf("aot=%d, freq_index=%d, channel=%d\n", audio_object_type, sampling_frequency_index, channel_config);

    int adtsLen = dataLen + 7;

    szAdtsHeader[0] = 0xff;         //syncword:0xfff                          高8bits
    szAdtsHeader[1] = 0xf0;         //syncword:0xfff                          低4bits
    szAdtsHeader[1] |= (0 << 3);    //MPEG Version:0 for MPEG-4,1 for MPEG-2  1bit
    szAdtsHeader[1] |= (0 << 1);    //Layer:0                                 2bits
    szAdtsHeader[1] |= 1;           //protection absent:1                     1bit

    szAdtsHeader[2] = (audio_object_type - 1)<<6;            //profile:audio_object_type - 1                      2bits
    szAdtsHeader[2] |= (sampling_frequency_index & 0x0f)<<2; //sampling frequency index:sampling_frequency_index  4bits
    szAdtsHeader[2] |= (0 << 1);                             //private bit:0                                      1bit
    szAdtsHeader[2] |= (channel_config & 0x04)>>2;           //channel configuration:channel_config               高1bit

    szAdtsHeader[3] = (channel_config & 0x03)<<6;     //channel configuration:channel_config      低2bits
    szAdtsHeader[3] |= (0 << 5);                      //original：0                               1bit
    szAdtsHeader[3] |= (0 << 4);                      //home：0                                   1bit
    szAdtsHeader[3] |= (0 << 3);                      //copyright id bit：0                       1bit
    szAdtsHeader[3] |= (0 << 2);                      //copyright id start：0                     1bit
    szAdtsHeader[3] |= ((adtsLen & 0x1800) >> 11);           //frame length：value   高2bits

    szAdtsHeader[4] = (uint8_t)((adtsLen & 0x7f8) >> 3);     //frame length:value    中间8bits
    szAdtsHeader[5] = (uint8_t)((adtsLen & 0x7) << 5);       //frame length:value    低3bits
    szAdtsHeader[5] |= 0x1f;                                 //buffer fullness:0x7ff 高5bits
    szAdtsHeader[6] = 0xfc;
}

int DecodeUnit::get_audio_obj_type(int aactype){
    //AAC HE V2 = AAC LC + SBR + PS
    //AAV HE = AAC LC + SBR
    //所以无论是 AAC_HEv2 还是 AAC_HE 都是 AAC_LC
    switch(aactype){
    case 0:
    case 2:
    case 3:
        return aactype+1;
    case 1:
    case 4:
    case 28:
        return 2;
    default:
        return 2;

    }
}

int DecodeUnit::get_sample_rate_index(int freq, int aactype){

    int i = 0;
    int freq_arr[13] = {
        96000, 88200, 64000, 48000, 44100, 32000,
        24000, 22050, 16000, 12000, 11025, 8000, 7350
    };

    //如果是 AAC HEv2 或 AAC HE, 则频率减半
    if(aactype == 28 || aactype == 4){
        freq /= 2;
    }

    for(i=0; i< 13; i++){
        if(freq == freq_arr[i]){
            return i;
        }
    }
    return 4;//默认是44100
}

int DecodeUnit::get_channel_config(int channels, int aactype){
    //如果是 AAC HEv2 通道数减半
    if(aactype == 28){
        return (channels / 2);
    }
    return channels;
}

