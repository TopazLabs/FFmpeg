/*
 * Copyright (c) 2012-2014 Clément Bœsch <u pkh me>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * Video Enhance AI filter
 *
 * @see https://www.topazlabs.com/video-enhance-ai
 */

#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/avutil.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"
#include "veai_common.h"

typedef struct  {
    const AVClass *class;
    char *model;
    int device, extraThreads;
    double slowmo;
    double vram;
    int canDownloadModels;
    void* pFrameProcessor;
    unsigned int count;
    float fpsFactor;
    float position;
    AVRational frame_rate;
    long long previousPts;
    long long currentPts;
    int isApollo;
    int stats;
    int (*filterFunc)(AVFilterLink *, AVFrame *);
    AVFrame* previousFrame;
} VEAIFIContext;

#define OFFSET(x) offsetof(VEAIFIContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption veai_fi_options[] = {
    { "model", "Model short name", OFFSET(model), AV_OPT_TYPE_STRING, {.str="chr-1"}, .flags = FLAGS },
    { "device",  "Device index (Auto: -2, CPU: -1, GPU0: 0, ...)",  OFFSET(device),  AV_OPT_TYPE_INT, {.i64=-2}, -2, 8, FLAGS, "device" },
    { "instances",  "Number of extra model instances to use on device",  OFFSET(extraThreads),  AV_OPT_TYPE_INT, {.i64=0}, 0, 3, FLAGS, "instances" },
    { "download",  "Enable model downloading",  OFFSET(canDownloadModels),  AV_OPT_TYPE_INT, {.i64=1}, 0, 1, FLAGS, "canDownloadModels" },
    { "vram", "Max memory usage", OFFSET(vram), AV_OPT_TYPE_DOUBLE, {.dbl=1.0}, 0.1, 1, .flags = FLAGS, "vram"},
    { "slowmo",  "Slowmo factor of the input video",  OFFSET(slowmo),  AV_OPT_TYPE_DOUBLE, {.dbl=1.0}, 0.1, 16, FLAGS, "slowmo" },
    { "fps", "output's frame rate, same as input frame rate if value is invalid", OFFSET(frame_rate), AV_OPT_TYPE_VIDEO_RATE, {.str = "0"}, 0, INT_MAX, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(veai_fi);

static int filter_frame_chronos(AVFilterLink *inlink, AVFrame *in);
static int filter_frame_apollo(AVFilterLink *inlink, AVFrame *in);
int handlePostFlight(void* pProcessor, AVFilterLink *outlink, AVFrame *in, AVFilterContext* ctx);

static av_cold int init(AVFilterContext *ctx) {
    VEAIFIContext *veai = ctx->priv;
    av_log(ctx, AV_LOG_DEBUG, "Init with params: %s %d %d %lf %d/%d = %lf\n", veai->model, veai->device, veai->extraThreads, veai->slowmo, veai->frame_rate.num, veai->frame_rate.den, av_q2d(veai->frame_rate));
    veai->count = 0;
    veai->position = 0;
    veai->previousPts = 0;
    veai->currentPts = 0;
    veai->previousFrame = NULL;
    return 0;
}

static int config_props(AVFilterLink *outlink) {
    AVFilterContext *ctx = outlink->src;
    VEAIFIContext *veai = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    float threshold = 0.05;
    if(veai->frame_rate.num > 0) {
        AVRational frFactor = av_div_q(veai->frame_rate, inlink->frame_rate);
        veai->fpsFactor = 1/(veai->slowmo*av_q2d(frFactor));

    } else {
        outlink->frame_rate = inlink->frame_rate;
        veai->fpsFactor = 1/veai->slowmo;
    }
    av_log(ctx, AV_LOG_DEBUG, "Set time base to %d/%d %lf -> %d/%d %lf\n", inlink->time_base.num, inlink->time_base.den, av_q2d(inlink->time_base), outlink->time_base.num, outlink->time_base.den, av_q2d(outlink->time_base));
    av_log(ctx, AV_LOG_DEBUG, "Set frame rate to %lf -> %lf\n", av_q2d(inlink->frame_rate), av_q2d(outlink->frame_rate));
    av_log(ctx, AV_LOG_DEBUG, "Set fpsFactor to %lf generating %lf frames\n", veai->fpsFactor, 1/veai->fpsFactor);
    threshold = veai->fpsFactor*0.3;
    float params[2] = {threshold, 1/veai->fpsFactor};
    veai->isApollo = strncmp(veai->model, (char*)"apo", 3) == 0;
    veai->filterFunc = veai->isApollo ? filter_frame_apollo : filter_frame_chronos;
    veai->pFrameProcessor = ff_veai_verifyAndCreate(inlink, outlink, veai->isApollo ? (char*)"apo" : (char*)"chr", veai->model, ModelTypeFrameInterpolation, veai->device, veai->extraThreads, veai->vram, 1, veai->canDownloadModels, params, 2, ctx);
    outlink->time_base = inlink->time_base;
    outlink->frame_rate = veai->frame_rate.num > 0 ? veai->frame_rate : inlink->frame_rate;
    return veai->pFrameProcessor == NULL ? AVERROR(EINVAL) : 0;
}


static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_RGB48,
    AV_PIX_FMT_NONE
};

static int filter_frame_chronos(AVFilterLink *inlink, AVFrame *in) {
    AVFilterContext *ctx = inlink->dst;
    VEAIFIContext *veai = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out;
    IOBuffer ioBuffer;
    float location = 0;
    static int ocount = 0;
    ff_veai_prepareIOBufferInput(&ioBuffer, in, FrameTypeNormal, veai->count == 0);
    if(veai->pFrameProcessor == NULL || veai_process(veai->pFrameProcessor,  &ioBuffer)) {
        av_log(ctx, AV_LOG_ERROR, "The processing has failed adding a frame\n");
        av_frame_free(&in);
        return AVERROR(ENOSYS);
    }
    while(veai->position < veai->count) {
        out = ff_veai_prepareBufferOutput(outlink, &ioBuffer.output);
        location = veai->position - (veai->count - 1);
        av_log(ctx, AV_LOG_DEBUG, "Process frame %f on current %d at %f\n", veai->position, veai->count, location);
        if(veai->pFrameProcessor == NULL || out == NULL || veai_interpolator_process(veai->pFrameProcessor, location, &ioBuffer)) {
            av_log(ctx, AV_LOG_ERROR, "The processing has failed for intermediate frame\n");
            av_frame_free(&in);
            return AVERROR(ENOSYS);
        }
        av_frame_copy_props(out, in);
        out->pts = ((in->pts - veai->previousPts)*location + in->pts)*veai->slowmo;
        ocount++;
        if(ff_filter_frame(outlink, out)) {
            av_frame_free(&in);
            return AVERROR(ENOSYS);
        }
        veai->position += veai->fpsFactor;
        av_log(ctx, AV_LOG_DEBUG, "Added frame at pts %lld %lf %d\n", out->pts, av_q2d(inlink->time_base)*out->pts, ocount);
    }
    veai->previousPts = in->pts;
    av_frame_free(&in);
    veai->count++;
    return 0;
}


static int filter_frame_apollo(AVFilterLink *inlink, AVFrame *in) {
    AVFilterContext *ctx = inlink->dst;
    VEAIFIContext *veai = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out;
    IOBuffer ioBuffer;
    float location = 0;
    static int ocount = 0;
    ff_veai_prepareIOBufferInput(&ioBuffer, in, FrameTypeNormal, veai->count == 0);
    if(veai->pFrameProcessor == NULL || veai_process(veai->pFrameProcessor,  &ioBuffer)) {
        av_log(ctx, AV_LOG_ERROR, "The processing has failed adding a frame\n");
        av_frame_free(&in);
        return AVERROR(ENOSYS);
    }
    if (veai->count > 1) {
        while(veai->position < veai->count - 1) {
            out = ff_veai_prepareBufferOutput(outlink, &ioBuffer.output);
            location = veai->position - (veai->count - 2);
            av_log(ctx, AV_LOG_DEBUG, "Process frame %f on current %d at %f\n", veai->position, veai->count, location);
            if(veai->pFrameProcessor == NULL || out == NULL || veai_interpolator_process(veai->pFrameProcessor, location, &ioBuffer)) {
                av_log(ctx, AV_LOG_ERROR, "The processing has failed for intermediate frame\n");
                av_frame_free(&in);
                return AVERROR(ENOSYS);
            }
            av_frame_copy_props(out, in);
            out->pts = ((veai->currentPts - veai->previousPts)*location + veai->previousPts)*veai->slowmo;
            ocount++;
            if(ff_filter_frame(outlink, out)) {
                av_frame_free(&in);
                return AVERROR(ENOSYS);
            }
            veai->position += veai->fpsFactor;
            av_log(ctx, AV_LOG_DEBUG, "Added frame at pts %lld %lf %d\n", out->pts, av_q2d(inlink->time_base)*out->pts, ocount);
        }
    }
    if(veai->previousFrame)
      av_frame_free(&veai->previousFrame);
    veai->previousFrame = in;
    veai->previousPts = veai->currentPts;
    veai->currentPts = in->pts;
    veai->count++;
    return 0;
}

static int request_frame(AVFilterLink *outlink) {
    AVFilterContext *ctx = outlink->src;
    VEAIFIContext *veai = ctx->priv;
    int ret = ff_request_frame(ctx->inputs[0]);
    if (veai->isApollo && ret == AVERROR_EOF) {
        if(handlePostFlight(veai->pFrameProcessor, outlink, veai->previousFrame, ctx)) {
          av_log(NULL, AV_LOG_ERROR, "The postflight processing has failed");
          av_frame_free(&veai->previousFrame);
          return AVERROR(ENOSYS);
        }
        av_frame_free(&veai->previousFrame);
        av_log(ctx, AV_LOG_DEBUG, "End of file reached %s %d\n", veai->model, veai->pFrameProcessor == NULL);
    }
    return ret;
}

int handlePostFlight(void* pProcessor, AVFilterLink *outlink, AVFrame *in, AVFilterContext* ctx) {
    VEAIFIContext *veai = ctx->priv;
    IOBuffer ioBuffer;
    float location = 0;
    veai_end_stream(pProcessor);
    int i;
    static int ocount = 0;
    for(i=0; i<2; i++) {
        VEAIBuffer oBuffer;
        AVFrame *out = ff_veai_prepareBufferOutput(outlink, &oBuffer);
        if(pProcessor == NULL || out == NULL || veai_process_back(pProcessor, &oBuffer)) {
            av_log(ctx, AV_LOG_ERROR, "The processing has failed");
            av_frame_free(&in);
            return AVERROR(ENOSYS);
        }
        if (veai->count > 1) {
            while(veai->position < veai->count - 1) {
                out = ff_veai_prepareBufferOutput(outlink, &ioBuffer.output);
                location = veai->position - (veai->count - 2);
                av_log(ctx, AV_LOG_DEBUG, "Process frame %f on current %d at %f\n", veai->position, veai->count, location);
                if(veai->pFrameProcessor == NULL || out == NULL || veai_interpolator_process(veai->pFrameProcessor, location, &ioBuffer)) {
                    av_log(ctx, AV_LOG_ERROR, "The processing has failed for intermediate frame\n");
                    av_frame_free(&in);
                    return AVERROR(ENOSYS);
                }
                av_frame_copy_props(out, in);
                out->pts = ((veai->currentPts - veai->previousPts)*location + veai->previousPts)*veai->slowmo;
                if (out->pts < 0) 
                    break;
                ocount++;
                if(ff_filter_frame(outlink, out)) {
                    av_frame_free(&in);
                    return AVERROR(ENOSYS);
                }
                veai->position += veai->fpsFactor;
                av_log(ctx, AV_LOG_DEBUG, "Added frame at pts %lld %lf %d\n", out->pts, av_q2d(outlink->time_base)*out->pts, ocount);
            }
        }
        long long ptsDiff = veai->currentPts - veai->previousPts;
        veai->previousPts = veai->currentPts;
        veai->currentPts += ptsDiff;
        veai->count++;
    }
    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in) {
    return ((VEAIFIContext *)inlink->dst->priv)->filterFunc(inlink, in);
}

static av_cold void uninit(AVFilterContext *ctx) {
    VEAIFIContext *veai = ctx->priv;
    if(veai->pFrameProcessor)
      veai_destroy(veai->pFrameProcessor);
}

static const AVFilterPad veai_fi_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
};

static const AVFilterPad veai_fi_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = config_props,
        .request_frame = request_frame,
    },
};

const AVFilter ff_vf_veai_fi = {
    .name          = "veai_fi",
    .description   = NULL_IF_CONFIG_SMALL("Apply Video Enhance AI frame interpolation models."),
    .priv_size     = sizeof(VEAIFIContext),
    .init          = init,
    .uninit        = uninit,
    FILTER_INPUTS(veai_fi_inputs),
    FILTER_OUTPUTS(veai_fi_outputs),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .priv_class    = &veai_fi_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
