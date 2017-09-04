#pragma once
#ifndef _FFMPEG_H_
#define _FFMPEG_H_

#include <stdio.h>
#include <errno.h>
#include <stdint.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavfilter/avfiltergraph.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/avfilter.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libavutil/timestamp.h>
#include <libavutil/mathematics.h>
#include <libavutil/timestamp.h>
#include <libavutil/pixdesc.h>
#include <libavutil/audio_fifo.h>



enum log_level_enum
{
    QUIET=AV_LOG_QUIET,
    PANIC = AV_LOG_PANIC,
    FATAL = AV_LOG_FATAL,
    ERROR = AV_LOG_ERROR,
    WARNING = AV_LOG_WARNING,
    INFO = AV_LOG_INFO,
    VERBOSE = AV_LOG_VERBOSE,
    DEBUG = AV_LOG_DEBUG,
    TRACE = AV_LOG_TRACE,
};


typedef struct StreamContext {
    AVCodecContext *dec_ctx;
    AVCodecContext *enc_ctx;
} StreamContext;

typedef struct EncodeParam
{
    char *vcoder;
    char *acoder;
    int *vbitrate;
} EncodeParam;

typedef struct FilteringContext {
    AVFilterContext* buffersrc_ctx;
    AVFilterContext* buffersink_ctx;
    AVFilterGraph* filter_graph;
}FilteringContext;

enum log_level_enum getLogLevel();
void set_log_level(enum log_level_enum level);
int create_trans_task(char *inputfilename, char *outputpath);

#endif