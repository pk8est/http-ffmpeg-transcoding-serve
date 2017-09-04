#include "ffmpeg.h"

#include <libavutil/timestamp.h>

#define DEBUG_LOG(fmt, ...) av_log(NULL, AV_LOG_DEBUG, "[%s:%d] DEBUG: " fmt, __FILE__, __LINE__, ##__VA_ARGS__);
#define INFO_LOG(fmt, ...) av_log(NULL, AV_LOG_INFO, "[%s:%d] INFO: " fmt, __FILE__, __LINE__, ##__VA_ARGS__);
#define ERROR_LOG(fmt, ...) av_log(NULL, AV_LOG_ERROR, "[%s:%d] ERROR: " fmt, __FILE__, __LINE__, ##__VA_ARGS__);
#define WARNING_LOG(fmt, ...) av_log(NULL, AV_LOG_WARNING, "[%s:%d] WARNING: " fmt, __FILE__, __LINE__, ##__VA_ARGS__);
#define FATAL_LOG(fmt, ...) av_log(NULL, AV_LOG_FATAL, "[%s:%d] FATAL: " fmt, __FILE__, __LINE__, ##__VA_ARGS__);

FILE *fp;
static enum log_level_enum log_level = INFO;
static EncodeParam default_encode_param = {
    "libx264", "aac", 900000,
};

enum log_level_enum getLogLevel() {
    return log_level;
}

void set_log_level(enum log_level_enum level) {
    log_level = level;
}

void set_av_log_level() {
    av_log_set_level(log_level);
}

int open_input_file(const char *filename, AVFormatContext **ifmt_ctx, StreamContext **stream_ctx) {
    int ret;
    unsigned int i;

    if((ret = avformat_open_input(ifmt_ctx, filename, NULL, NULL)) < 0) {
        ERROR_LOG("avformat_open_input error: %s '%s'!\n", av_err2str(ret), filename);
        return ret;
    }

    if ((ret = avformat_find_stream_info(*ifmt_ctx, NULL)) < 0) {
        ERROR_LOG("avformat_find_stream error: %s!\n", av_err2str(ret));
        return ret;
    }

    *stream_ctx = av_mallocz_array((*ifmt_ctx)->nb_streams, sizeof(**stream_ctx));
    printf("stream_ctx=%s", *stream_ctx);
    if (!*stream_ctx)
        return AVERROR(ENOMEM);

    for (i = 0; i < (*ifmt_ctx)->nb_streams; i++) {
        AVStream *stream = (*ifmt_ctx)->streams[i];
        AVCodec *dec = avcodec_find_decoder(stream->codecpar->codec_id);
        AVCodecContext *codec_ctx;
        if (!dec) {
            ERROR_LOG("Failed to find decoder for stream #%u: %s!\n", i, av_err2str(ret));
            return AVERROR_DECODER_NOT_FOUND;
        }
        codec_ctx = avcodec_alloc_context3(dec);
        if (!codec_ctx) {
            av_log(NULL, AV_LOG_ERROR, "Failed to allocate the decoder context for stream #%u: %s!\n", i, av_err2str(ret));
            return AVERROR(ENOMEM);
        }
        ret = avcodec_parameters_to_context(codec_ctx, stream->codecpar);
        if (ret < 0) {
            ERROR_LOG("Failed to copy decoder parameters to input decoder context "
                "for stream #%u: %s!\n", i, av_err2str(ret));
            return ret;
        }
        /* Reencode video & audio and remux subtitles etc. */
        if (codec_ctx->codec_type == AVMEDIA_TYPE_VIDEO
            || codec_ctx->codec_type == AVMEDIA_TYPE_AUDIO) {
            if (codec_ctx->codec_type == AVMEDIA_TYPE_VIDEO)
                codec_ctx->framerate = av_guess_frame_rate(ifmt_ctx, stream, NULL);
            /* Open decoder */
            ret = avcodec_open2(codec_ctx, dec, NULL);
            if (ret < 0) {
                ERROR_LOG("Failed to open decoder for stream #%u: %s!\n", i, av_err2str(ret));
                return ret;
            }
        }
        (*stream_ctx)[i].dec_ctx = codec_ctx;
    }

    av_dump_format(*ifmt_ctx, 0, filename, 0);

    return 0;
}

void init_ffmpeg() {
    avfilter_register_all();
    av_register_all();
    return;
}

int open_output_file(const char *filename, const AVFormatContext *ifmt_ctx, AVFormatContext **ofmt_ctx, StreamContext **stream_ctx) {
    AVStream *out_stream;
    AVStream *in_stream;
    AVCodecContext *dec_ctx, *enc_ctx;
    AVCodec *encoder;
    int ret;
    unsigned int i;

    avformat_alloc_output_context2(ofmt_ctx, NULL, "mpegts", filename);
    if (!*ofmt_ctx) {
        ERROR_LOG("Could not create output context: %s!\n", av_err2str(AVERROR_UNKNOWN));
        return AVERROR_UNKNOWN;
    }

    for (i = 0; i < ifmt_ctx->nb_streams; i++) {
        out_stream = avformat_new_stream(*ofmt_ctx, NULL);
        if (!out_stream) {
            ERROR_LOG("Failed allocating output stream %s!\n", av_err2str(AVERROR_UNKNOWN));
            return AVERROR_UNKNOWN;
        }


        in_stream = ifmt_ctx->streams[i];
        dec_ctx = (*stream_ctx)[i].dec_ctx;

        if (dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO || dec_ctx->codec_type == AVMEDIA_TYPE_AUDIO) {
            // Set Option
            AVDictionary *param = 0;

            INFO_LOG("reopen decoder,stream %d\n", i);
            if (dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO) {
                //encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
                encoder = avcodec_find_encoder_by_name("libx264");
            }else {
                //encoder = avcodec_find_encoder(AV_CODEC_ID_AAC);
                encoder = avcodec_find_encoder(dec_ctx->codec->id);
            }

            if (encoder == NULL) {
                ERROR_LOG("not support encoder type: %s!\n", av_err2str(AVERROR_INVALIDDATA));
                return AVERROR_INVALIDDATA;
            }

            enc_ctx = avcodec_alloc_context3(encoder);
            if (!enc_ctx) {
                ERROR_LOG("Failed to allocate the encoder context: %s!\n", av_err2str(AVERROR(ENOMEM)));
                return AVERROR(ENOMEM);
            }

            if (dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO) {
                enc_ctx->width = dec_ctx->width;
                enc_ctx->height = dec_ctx->height;
                enc_ctx->sample_aspect_ratio = dec_ctx->sample_aspect_ratio;
                if (encoder->pix_fmts) {
                    enc_ctx->pix_fmt = encoder->pix_fmts[0];
                }
                enc_ctx->time_base = dec_ctx->time_base;
                enc_ctx->codec_id = encoder->id;
                enc_ctx->codec_type = encoder->type;
                enc_ctx->me_range = 16;
                enc_ctx->qcompress = 0.6;
                enc_ctx->bit_rate = 880000;
                //enc_ctx->qmin = 30;//决定文件大小，qmin越大，编码压缩率越高
                //enc_ctx->qmax = 40;
                enc_ctx->me_subpel_quality = 1;//决定编码速度，越小，编码速度越快
                enc_ctx->has_b_frames = 0;
                enc_ctx->max_b_frames = 0;
            }else {
                
                enc_ctx->sample_rate = dec_ctx->sample_rate;
                enc_ctx->channel_layout = dec_ctx->channel_layout;
                enc_ctx->channels = av_get_channel_layout_nb_channels(enc_ctx->channel_layout);
                if (encoder->sample_fmts) {
                    enc_ctx->sample_fmt = encoder->sample_fmts[0];
                }
                enc_ctx->time_base = (AVRational) { 1, enc_ctx->sample_rate };
                
            }
            
            //H.264
            //if (enc_ctx->codec_id == AV_CODEC_ID_H264) {
                //av_dict_set(&param, "preset", "slow", 0);
                //av_dict_set(&param, "tune", "zerolatency", 0);
                //av_dict_set(&param, "profile", "main", 0);
            //}
            //av_opt_set(enc_ctx->priv_data, "hls_time", "10");
            //x264_param_default_preset(&params, "ultrafast", "stillimage,zerolatency");

            
            /* set options */
            /*
            av_opt_set_int(ost->swr_ctx, "in_channel_count", c->channels, 0);
            av_opt_set_int(ost->swr_ctx, "in_sample_rate", c->sample_rate, 0);
            av_opt_set_sample_fmt(ost->swr_ctx, "in_sample_fmt", AV_SAMPLE_FMT_S16, 0);
            av_opt_set_int(ost->swr_ctx, "out_channel_count", c->channels, 0);
            av_opt_set_int(ost->swr_ctx, "out_sample_rate", c->sample_rate, 0);
            av_opt_set_sample_fmt(ost->swr_ctx, "out_sample_fmt", c->sample_fmt, 0);
            */

            ret = avcodec_open2(enc_ctx, encoder, &param);
            if (ret < 0) {
                ERROR_LOG("Cannot open video encoder for stream #%u: %s!\n", i,av_err2str(ret));
                return ret;
            }
            ret = avcodec_parameters_from_context(out_stream->codecpar, enc_ctx);
            if (ret < 0) {
                ERROR_LOG("Failed to copy encoder parameters to output stream #%u: %s!\n", i, av_err2str(ret));
                return ret;
            }
            if ((*ofmt_ctx)->oformat->flags & AVFMT_GLOBALHEADER)
                enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

            out_stream->time_base = in_stream->time_base;
            (*stream_ctx)[i].enc_ctx = enc_ctx;
        }else if (dec_ctx->codec_type == AVMEDIA_TYPE_UNKNOWN) {
            FATAL_LOG("Elementary stream #%d is of unknown type, cannot proceed: %s!\n", i, av_err2str(AVERROR_INVALIDDATA));
            return AVERROR_INVALIDDATA;
        }else {
            /* if this stream must be remuxed */
            ret = avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar);
            if (ret < 0) {
                ERROR_LOG("Copying parameters for stream #%u failed: %s!\n", i, av_err2str(ret));
                return ret;
            }
            out_stream->time_base = in_stream->time_base;
        }
    }
    av_dump_format(*ofmt_ctx, 0, filename, 1);

    if (!((*ofmt_ctx)->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&(*ofmt_ctx)->pb, filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            ERROR_LOG( "Could not open output file '%s': %s!\n", filename, av_err2str(ret));
            return ret;
        }
    }

    /* init muxer, write output file header */
    ret = avformat_write_header(*ofmt_ctx, NULL);
    if (ret < 0) {
        ERROR_LOG("Error occurred when opening output file: %s!\n", av_err2str(ret));
        return ret;
    }

    return 0;
}

int init_filter(FilteringContext *fctx, AVCodecContext *dec_ctx, AVCodecContext *enc_ctx, const char *filter_spec) {
    char args[512];
    int ret = 0;
    AVFilter *buffersrc = NULL;
    AVFilter *buffersink = NULL;
    AVFilterContext *buffersrc_ctx = NULL;
    AVFilterContext *buffersink_ctx = NULL;
    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs = avfilter_inout_alloc();
    AVFilterGraph *filter_graph = avfilter_graph_alloc();

    if (!outputs || !inputs || !filter_graph) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    if (dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO) {
        buffersrc = avfilter_get_by_name("buffer");
        buffersink = avfilter_get_by_name("buffersink");
        if (!buffersrc || !buffersink) {
            av_log(NULL, AV_LOG_ERROR, "filtering source or sink element not found\n");
            ret = AVERROR_UNKNOWN;
            goto end;
        }

        snprintf(args, sizeof(args),
            "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
            dec_ctx->width, dec_ctx->height, dec_ctx->pix_fmt,
            dec_ctx->time_base.num, dec_ctx->time_base.den,
            dec_ctx->sample_aspect_ratio.num,
            dec_ctx->sample_aspect_ratio.den);

        ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in",
            args, NULL, filter_graph);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot create buffer source\n");
            goto end;
        }

        ret = avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out",
            NULL, NULL, filter_graph);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot create buffer sink\n");
            goto end;
        }

        ret = av_opt_set_bin(buffersink_ctx, "pix_fmts",
            (uint8_t*)&enc_ctx->pix_fmt, sizeof(enc_ctx->pix_fmt),
            AV_OPT_SEARCH_CHILDREN);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot set output pixel format\n");
            goto end;
        }
    }else if (dec_ctx->codec_type == AVMEDIA_TYPE_AUDIO) {
        buffersrc = avfilter_get_by_name("abuffer");
        buffersink = avfilter_get_by_name("abuffersink");
        if (!buffersrc || !buffersink) {
            av_log(NULL, AV_LOG_ERROR, "filtering source or sink element not found\n");
            ret = AVERROR_UNKNOWN;
            goto end;
        }

        if (!dec_ctx->channel_layout)
            dec_ctx->channel_layout =
            av_get_default_channel_layout(dec_ctx->channels);
        snprintf(args, sizeof(args),
            "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=0x%"PRIx64,
            dec_ctx->time_base.num, dec_ctx->time_base.den, dec_ctx->sample_rate,
            av_get_sample_fmt_name(dec_ctx->sample_fmt),
            dec_ctx->channel_layout);
        ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in",
            args, NULL, filter_graph);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot create audio buffer source\n");
            goto end;
        }

        ret = avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out",
            NULL, NULL, filter_graph);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot create audio buffer sink\n");
            goto end;
        }

        ret = av_opt_set_bin(buffersink_ctx, "sample_fmts",
            (uint8_t*)&enc_ctx->sample_fmt, sizeof(enc_ctx->sample_fmt),
            AV_OPT_SEARCH_CHILDREN);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot set output sample format\n");
            goto end;
        }

        ret = av_opt_set_bin(buffersink_ctx, "channel_layouts",
            (uint8_t*)&enc_ctx->channel_layout,
            sizeof(enc_ctx->channel_layout), AV_OPT_SEARCH_CHILDREN);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot set output channel layout\n");
            goto end;
        }

        ret = av_opt_set_bin(buffersink_ctx, "sample_rates",
            (uint8_t*)&enc_ctx->sample_rate, sizeof(enc_ctx->sample_rate),
            AV_OPT_SEARCH_CHILDREN);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot set output sample rate\n");
            goto end;
        }
    }else {
        ret = AVERROR_UNKNOWN;
        goto end;
    }

    /* Endpoints for the filter graph. */
    outputs->name = av_strdup("in");
    outputs->filter_ctx = buffersrc_ctx;
    outputs->pad_idx = 0;
    outputs->next = NULL;

    inputs->name = av_strdup("out");
    inputs->filter_ctx = buffersink_ctx;
    inputs->pad_idx = 0;
    inputs->next = NULL;


    if (!outputs->name || !inputs->name) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    if ((ret = avfilter_graph_parse_ptr(filter_graph, filter_spec,
        &inputs, &outputs, NULL)) < 0)
        goto end;

    if ((ret = avfilter_graph_config(filter_graph, NULL)) < 0)
        goto end;

    /* Fill FilteringContext */
    fctx->buffersrc_ctx = buffersrc_ctx;
    fctx->buffersink_ctx = buffersink_ctx;
    fctx->filter_graph = filter_graph;


end:
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);

    return ret;
}

int init_filters(const AVFormatContext *ifmt_ctx, const AVFormatContext *ofmt_ctx, const StreamContext *stream_ctx, FilteringContext **filter_ctx) {
    int ret;
    unsigned int i;
    const char *filter_spec;

    *filter_ctx = av_malloc_array(ifmt_ctx->nb_streams, sizeof(**filter_ctx));
    if (!*filter_ctx) {
        ERROR_LOG("create filtering context error: %s!\n", av_err2str(AVERROR(ENOMEM)));
        return AVERROR(ENOMEM);
    }

    for (i = 0; i < ifmt_ctx->nb_streams; i++) {
        (*filter_ctx)[i].buffersrc_ctx = NULL;
        (*filter_ctx)[i].buffersink_ctx = NULL;
        (*filter_ctx)[i].filter_graph = NULL;
        if (!(ifmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO
            || ifmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO))
            continue;


        if (ifmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
            //filter_spec = "null";
            filter_spec = "movie=./build/logo.png[wm];[in][wm]overlay=5:5[out]"; /* passthrough (dummy) filter for video */
        else
            filter_spec = "anull"; /* passthrough (dummy) filter for audio */
        ret = init_filter(&(*filter_ctx)[i], stream_ctx[i].dec_ctx, stream_ctx[i].enc_ctx, filter_spec);
        if (ret)
            return ret;
    }

    return 0;
}

int encode_video(const AVFormatContext *ifmt_ctx, const AVFormatContext *ofmt_ctx, const StreamContext *stream_ctx,
    AVFrame *filt_frame, unsigned int stream_index, int *got_frame) {
    int ret;
    int got_frame_local;
    AVPacket enc_pkt;

    int(*enc_func)(AVCodecContext *, AVPacket *, const AVFrame *, int *) =
        (ifmt_ctx->streams[stream_index]->codecpar->codec_type ==
            AVMEDIA_TYPE_VIDEO) ? avcodec_encode_video2 : avcodec_encode_audio2;

    if (!got_frame)
        got_frame = &got_frame_local;

    DEBUG_LOG("Encoding frame\n");
    /* encode filtered frame */
    enc_pkt.data = NULL;
    enc_pkt.size = 0;
    av_init_packet(&enc_pkt);
    ret = enc_func(stream_ctx[stream_index].enc_ctx, &enc_pkt,
        filt_frame, got_frame);
    av_frame_free(&filt_frame);
    if (ret < 0)
        return ret;
    if (!(*got_frame))
        return 0;

    /* prepare packet for muxing */
    enc_pkt.stream_index = stream_index;
    av_packet_rescale_ts(&enc_pkt,
        stream_ctx[stream_index].enc_ctx->time_base,
        ofmt_ctx->streams[stream_index]->time_base);

    DEBUG_LOG("Muxing frame\n");
    /* mux encoded frame */
    ret = av_write_frame(ofmt_ctx, &enc_pkt);
    //fwrite(enc_pkt.data, 1, enc_pkt.size, fp);
    return ret;
}

int encode_video2(const AVFormatContext *ifmt_ctx, const AVFormatContext *ofmt_ctx, const StreamContext *stream_ctx,
    AVFrame *filt_frame, unsigned int stream_index, int *got_frame) {
    int ret;
    int got_frame_local;
    AVPacket enc_pkt;

    AVCodecContext *enc_ctx = stream_ctx[stream_index].enc_ctx;

    /* encode filtered frame */
    enc_pkt.data = NULL;
    enc_pkt.size = 0;
    av_init_packet(&enc_pkt);

    /*
    av_log(NULL, AV_LOG_INFO, "encoder <- type:video "
        "frame_pts:%s frame_pts_time:%s time_base:%d/%d key_frame:%d pict_type: %d\n",
        av_ts2str(filt_frame->pts), av_ts2timestr(filt_frame->pts, &enc_ctx->time_base),
        enc_ctx->time_base.num, enc_ctx->time_base.den, filt_frame->key_frame, filt_frame->pict_type);
    */

    ret = avcodec_send_frame(enc_ctx, filt_frame);
    if (ret < 0) {
        fprintf(stderr, "Error sending a frame for encoding\n");
        return ret;
    }

    while (ret >= 0) {
        ret = avcodec_receive_packet(enc_ctx, &enc_pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return ret;
        else if (ret < 0) {
            fprintf(stderr, "Error during encoding\n");
            return ret;
        }
        enc_pkt.stream_index = stream_index;
        av_packet_rescale_ts(&enc_pkt, enc_ctx->time_base, ofmt_ctx->streams[stream_index]->time_base);
        //printf("Video %d => %d \n", enc_pkt.duration, enc_pkt.dts);
        //printf("Write packet %3"PRId64" (size=%5d)\n", enc_pkt.pts, enc_pkt.size);
        ret = av_interleaved_write_frame(ofmt_ctx, &enc_pkt);
        if (ret < 0) {
            fprintf(stderr, "Error av_write_frame\n");
        }
        //fwrite(enc_pkt->data, 1, enc_pkt->size, outfile);
        av_packet_unref(&enc_pkt);
    }

    av_frame_free(&filt_frame);

    return 0;
}

int encode_audio(const AVFormatContext *ifmt_ctx, const AVFormatContext *ofmt_ctx, const StreamContext *stream_ctx,
    AVFrame *filt_frame, unsigned int stream_index, int *got_frame) {
    int ret;
    int got_frame_local;
    AVPacket enc_pkt;

    int(*enc_func)(AVCodecContext *, AVPacket *, const AVFrame *, int *) =
        (ifmt_ctx->streams[stream_index]->codecpar->codec_type ==
            AVMEDIA_TYPE_VIDEO) ? avcodec_encode_video2 : avcodec_encode_audio2;

    if (!got_frame)
        got_frame = &got_frame_local;

    DEBUG_LOG("Encoding frame\n");
    /* encode filtered frame */
    enc_pkt.data = NULL;
    enc_pkt.size = 0;
    av_init_packet(&enc_pkt);
    ret = enc_func(stream_ctx[stream_index].enc_ctx, &enc_pkt,
        filt_frame, got_frame);
    
    av_frame_free(&filt_frame);
    if (ret < 0)
        return ret;
    if (!(*got_frame))
        return 0;

    /* prepare packet for muxing */
    enc_pkt.stream_index = stream_index;

    DEBUG_LOG("Muxing frame\n");
    /* mux encoded frame */
    ret = av_write_frame(ofmt_ctx, &enc_pkt);
    return ret;
}

int encode_audio2(const AVFormatContext *ifmt_ctx, const AVFormatContext *ofmt_ctx, const StreamContext *stream_ctx,
    AVFrame *filt_frame, unsigned int stream_index, int *got_frame) {
    int ret;
    int got_frame_local;
    AVPacket enc_pkt;

    AVCodecContext *enc_ctx = stream_ctx[stream_index].enc_ctx;

    /* encode filtered frame */
    enc_pkt.data = NULL;
    enc_pkt.size = 0;
    av_init_packet(&enc_pkt);

    /*av_log(NULL, AV_LOG_INFO, "encoder <- type:audio "
    "frame_pts:%s frame_pts_time:%s time_base:%d/%d\n",
    av_ts2str(filt_frame->pts), av_ts2timestr(filt_frame->pts, &enc_ctx->time_base),
    enc_ctx->time_base.num, enc_ctx->time_base.den);*/

    ret = avcodec_send_frame(enc_ctx, filt_frame);
    if (ret < 0) {
        fprintf(stderr, "Error sending a frame for encoding\n");
        return ret; 
    }

    /* read all the available output packets (in general there may be any
    * number of them */
    while (ret >= 0) {
        ret = avcodec_receive_packet(enc_ctx, &enc_pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            return ret;
        }
        else if (ret < 0) {
            fprintf(stderr, "Error encoding audio frame\n");
            return ret;
        }
        enc_pkt.stream_index = stream_index;
        av_packet_rescale_ts(&enc_pkt, enc_ctx->time_base, ofmt_ctx->streams[stream_index]->time_base);

        /*av_log(NULL, AV_LOG_INFO, "encoder -> type:audio "
            "pkt_pts:%s pkt_pts_time:%s pkt_dts:%s pkt_dts_time:%s\n",
            av_ts2str(enc_pkt.pts), av_ts2timestr(enc_pkt.pts, &enc_ctx->time_base),
            av_ts2str(enc_pkt.dts), av_ts2timestr(enc_pkt.dts, &enc_ctx->time_base));*/

        ret = av_interleaved_write_frame(ofmt_ctx, &enc_pkt);
        if (ret < 0) {
            fprintf(stderr, "Error audio av_write_frame\n");
        }

        av_packet_unref(&enc_pkt);
    }

    av_frame_free(&filt_frame);

    return 0;
}

int encode_write_frame(const AVFormatContext *ifmt_ctx, const AVFormatContext *ofmt_ctx, const StreamContext *stream_ctx,
    AVFrame *filt_frame, unsigned int stream_index, int *got_frame) {
    if (ifmt_ctx->streams[stream_index]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
        return encode_audio2(ifmt_ctx, ofmt_ctx, stream_ctx, filt_frame, stream_index, got_frame);
    }else {
        return encode_video2(ifmt_ctx, ofmt_ctx, stream_ctx, filt_frame, stream_index, got_frame);
    }
}

int filter_encode_write_frame(const AVFormatContext *ifmt_ctx, const AVFormatContext *ofmt_ctx, const StreamContext *stream_ctx, 
    const FilteringContext *filter_ctx, AVFrame *frame, unsigned int stream_index) {
    int ret;
    AVFrame *filt_frame;

    DEBUG_LOG("Pushing decoded frame to filters!\n");
    
    ret = av_buffersrc_add_frame_flags(filter_ctx[stream_index].buffersrc_ctx, frame, 0);
    if (ret < 0) {
        ERROR_LOG("Error while feeding the filtergraph,%s,frame exist is %d\n", av_err2str(ret), frame != NULL);
        return ret;
    }
    
    while (1) {
        filt_frame = av_frame_alloc();
        if (!filt_frame) {
            ret = AVERROR(ENOMEM);
            break;
        }
        
        DEBUG_LOG("Pulling filtered frame from filters!\n");
        ret = av_buffersink_get_frame(filter_ctx[stream_index].buffersink_ctx, filt_frame);
        if (ret < 0) {
            /* if no more frames for output - returns AVERROR(EAGAIN)
            * if flushed and no more frames for output - returns AVERROR_EOF
            * rewrite retcode to 0 to show it as normal procedure completion
            */
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                ret = 0;
            av_frame_free(&filt_frame);
            break;
        }
        filt_frame->pict_type = AV_PICTURE_TYPE_NONE;
        ret = encode_write_frame(ifmt_ctx, ofmt_ctx, stream_ctx, filt_frame, stream_index, NULL);
        if (ret < 0)
            break;
    }

    return 0;
}

static int flush_encoder(const AVFormatContext *ifmt_ctx, const AVFormatContext *ofmt_ctx, const StreamContext *stream_ctx, unsigned int stream_index)
{
    int ret;
    int got_frame;

    if (!(stream_ctx[stream_index].enc_ctx->codec->capabilities &
        AV_CODEC_CAP_DELAY))
        return 0;

    while (1) {
        DEBUG_LOG("Flushing stream #%u encoder\n", stream_index);
        ret = encode_write_frame(ifmt_ctx, ofmt_ctx, stream_ctx, NULL, stream_index, &got_frame);
        if (ret < 0)
            break;
        if (!got_frame)
            return 0;
    }
    return ret;
}

void print_info(const AVFormatContext *ifmt_ctx) {
    printf("filename: %s\n", ifmt_ctx->filename);
    int iTotalSeconds = (int)ifmt_ctx->duration/*微秒*/ / 1000000;
    int iHour = iTotalSeconds / 3600;//小时  
    int iMinute = iTotalSeconds % 3600 / 60;//分钟  
    int iSecond = iTotalSeconds % 60;//秒  
    AVDictionaryEntry *dict = NULL;
    AVCodecContext *pCodecCtx = NULL;
    AVCodec *pCodec;

    pCodecCtx = ifmt_ctx->streams[0]->codec;   //指向AVCodecContext的指针  
    pCodec = avcodec_find_decoder(pCodecCtx->codec_id);  //指向AVCodec的指针.查找解码器  

    printf("持续时间：%02d:%02d:%02d\n", iHour, iMinute, iSecond);

    puts("---------------------------------------------");

    puts("AVInputFormat信息:");
    puts("---------------------------------------------");
    printf("封装格式名称：%s\n", ifmt_ctx->iformat->name);
    printf("封装格式长名称：%s\n", ifmt_ctx->iformat->long_name);
    printf("封装格式扩展名：%s\n", ifmt_ctx->iformat->extensions);
    printf("封装格式ID：%d\n", ifmt_ctx->iformat->raw_codec_id);
    puts("---------------------------------------------");

    puts("AVStream信息:");
    puts("---------------------------------------------");
    printf("视频流标识符：%d\n", ifmt_ctx->streams[0]->index);
    printf("音频流标识符：%d\n", ifmt_ctx->streams[1]->index);
    printf("视频流长度：%d微秒\n", ifmt_ctx->streams[0]->duration);
    printf("音频流长度：%d微秒\n", ifmt_ctx->streams[1]->duration);
    puts("---------------------------------------------");

    puts("AVCodecContext信息:");
    puts("---------------------------------------------");
    printf("视频码率：%d kb/s\n", pCodecCtx->bit_rate / 1000);
    printf("视频大小：%d * %d\n", pCodecCtx->width, pCodecCtx->height);
    puts("---------------------------------------------");

    puts("AVCodec信息:");
    puts("---------------------------------------------");
    printf("视频编码格式：%s\n", pCodec->name);
    printf("视频编码详细格式：%s\n", pCodec->long_name);
    puts("---------------------------------------------");

    printf("视频时长：%d微秒\n", ifmt_ctx->streams[0]->duration);
    printf("音频时长：%d微秒\n", ifmt_ctx->streams[1]->duration);
    printf("音频采样率：%d\n", ifmt_ctx->streams[1]->codec->sample_rate);
    printf("音频信道数目：%d\n", ifmt_ctx->streams[1]->codec->channels);

    puts("AVFormatContext元数据：");
    puts("---------------------------------------------");
    dict = NULL;
    while (dict = av_dict_get(ifmt_ctx->streams[0]->metadata, "", dict, AV_DICT_IGNORE_SUFFIX))
    {
        printf("[%s] = %s\n", dict->key, dict->value);
    }
    puts("---------------------------------------------");

    puts("AVStream音频元数据：");
    puts("---------------------------------------------");
    dict = NULL;
    while (dict = av_dict_get(ifmt_ctx->streams[1]->metadata, "", dict, AV_DICT_IGNORE_SUFFIX))
    {
        printf("[%s] = %s\n", dict->key, dict->value);
    }
    puts("---------------------------------------------");

    printf("\n\n编译信息：\n%s\n\n", avcodec_configuration());
    return 0;
}

static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt, const char *tag)
{
    AVRational *time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;

    printf("%s: pts:%s pts_time:%s dts:%s dts_time:%s duration:%s duration_time:%s stream_index:%d\n",
        tag,
        av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, time_base),
        av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, time_base),
        av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, time_base),
        pkt->stream_index);
}

int create_trans_task(char *input_filename, char *output_filename) {

    int ret, i, j, thread_ret;
    int got_frame = 0;
    int ts = 0;
    int thread_count = 5;
    int start = 1;
    int stream_index;
    int index = 0;
    AVFormatContext *ifmt_ctx = NULL;
    AVFormatContext *ofmt_ctx = NULL;
    StreamContext *stream_ctx = NULL;
    FilteringContext *filter_ctx = NULL;
    enum AVMediaType type;
    AVPacket packet = { .data = NULL,.size = 0 };
    AVFrame *frame = NULL;
    int(*dec_func)(AVCodecContext *, AVFrame *, int *, const AVPacket *);

    fp = fopen("./output-pipe.ts", "wb");

    //freopen("output.ts", "w", stdout); //输出重定向，输出数据将保存在out.txt文件中 
    //freopen("output.txt", "w", stderr); //输出重定向，输出数据将保存在out.txt文件中 


    if(input_filename == NULL || output_filename == NULL){
        return -1;
    }

    init_ffmpeg();
    set_av_log_level();

    if((ret = open_input_file(input_filename, &ifmt_ctx, &stream_ctx)) < 0){
        goto end;
    }

    print_info(ifmt_ctx);

    if((ret = open_output_file(output_filename, ifmt_ctx, &ofmt_ctx, &stream_ctx)) < 0){
        goto end;
    }

    if ((ret = init_filters(ifmt_ctx, ofmt_ctx, stream_ctx, &filter_ctx)) < 0) {
        goto end;
    }

    while (1){
        if ((ret = av_read_frame(ifmt_ctx, &packet)) < 0) {
            if (ret == AVERROR_EOF) {
                INFO_LOG("read inputfile frame over!\n");
                break;
            }else{
                ERROR_LOG("read inputfile frame error: %s!", av_err2str(ret));
                continue;
            }
        }
        stream_index = packet.stream_index;
        type = ifmt_ctx->streams[stream_index]->codecpar->codec_type;
        DEBUG_LOG("Demuxer gave frame of stream_index %u!\n", stream_index);
        DEBUG_LOG("Going to reencode&filter the frame %u!\n", stream_index);

        frame = av_frame_alloc();
        if (!frame) {
            ERROR_LOG("alloc frame error!\n");
            ret = AVERROR(ENOMEM);
            av_packet_unref(&packet);
            break;
        }

        av_packet_rescale_ts(&packet,
            ifmt_ctx->streams[stream_index]->time_base,
            stream_ctx[stream_index].dec_ctx->time_base);

        /*************************/

        AVCodecContext *dec_ctx, *enc_ctx;
        stream_index = packet.stream_index;
        dec_ctx = stream_ctx[stream_index].dec_ctx;
        enc_ctx = stream_ctx[stream_index].enc_ctx;

        ret = avcodec_send_packet(dec_ctx, &packet);
        if (ret < 0 && ret != AVERROR_EOF) {
            ERROR_LOG("avcodec_send_packet fail %d\n", ret);
            continue;
        }

        while (ret >= 0) {
            
            ret = avcodec_receive_frame(dec_ctx, frame);

            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            }
            else if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR, "Error while receiving a frame from the decoder\n");
                goto end;
            }

            if (ret >= 0) {
                
                //frame->pts = frame->best_effort_timestamp;  
                frame->pts = av_frame_get_best_effort_timestamp(frame);
                ret = filter_encode_write_frame(ifmt_ctx, ofmt_ctx, stream_ctx, filter_ctx, frame, stream_index);
                av_frame_free(&frame);
                if (ret < 0) {
                    goto end;
                }
            }
            else {
                av_frame_free(&frame);
            }

        }
        
    }

    /* flush filters and encoders */
    for (i = 0; i < ifmt_ctx->nb_streams; i++) {
        /* flush filter */
        if (!filter_ctx[i].filter_graph)
            continue;
        ret = filter_encode_write_frame(ifmt_ctx, ofmt_ctx, stream_ctx, filter_ctx, NULL, i);
        if (ret < 0) {
            ERROR_LOG("Flushing filter failed: %s!\n", av_err2str(ret));
            goto end;
        }

        /* flush encoder */
        ret = flush_encoder(ifmt_ctx, ofmt_ctx, stream_ctx, i);
        if (ret < 0) {
            ERROR_LOG("Flushing encoder failed: %s!\n", av_err2str(ret));
            goto end;
        }
    }

    av_write_trailer(ofmt_ctx);

end:
    av_packet_unref(&packet);
    av_frame_free(&frame);
    for (i = 0; i < ifmt_ctx->nb_streams; i++) {
        avcodec_free_context(&stream_ctx[i].dec_ctx);
        if (ofmt_ctx && ofmt_ctx->nb_streams > i && ofmt_ctx->streams[i] && stream_ctx[i].enc_ctx)
            avcodec_free_context(&stream_ctx[i].enc_ctx);
        if (filter_ctx && filter_ctx[i].filter_graph)
            avfilter_graph_free(&filter_ctx[i].filter_graph);
    }
    av_free(filter_ctx);
    av_free(stream_ctx);
    avformat_close_input(&ifmt_ctx);
    if (ofmt_ctx && !(ofmt_ctx->oformat->flags & AVFMT_NOFILE))
        avio_closep(&ofmt_ctx->pb);
    avformat_free_context(ofmt_ctx);

    return 0;
}