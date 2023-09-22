/*
 * Copyright (c) 2022 Topaz Labs LLC
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
 * Topaz Video AI Frame Interpolation filter
 *
 * @see https://www.topazlabs.com/topaz-video-ai
 */

#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/avutil.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"
#include "tvai_common.h"

typedef struct  {
    const AVClass *class;
    BasicProcessorInfo basicInfo;
    char *model;
    int device, extraThreads;
    double slowmo;
    double vram;
    int canDownloadModels;
    double rdt;
    void* pFrameProcessor;
    AVRational frame_rate;
    AVFrame* previousFrame;
} TVAIFIContext;

#define OFFSET(x) offsetof(TVAIFIContext, x)
#define BASIC_OFFSET(x) offsetof(TVAIFIContext, basicInfo) + offsetof(BasicProcessorInfo, x)
#define DEVICE_OFFSET(x) BASIC_OFFSET(device) + offsetof(DeviceSetting, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption tvai_fi_options[] = {
    { "model", "Model short name", BASIC_OFFSET(modelName), AV_OPT_TYPE_STRING, {.str="chr-1"}, .flags = FLAGS },
    { "device",  "Device index (Auto: -2, CPU: -1, GPU0: 0, ...)",  DEVICE_OFFSET(index),  AV_OPT_TYPE_INT, {.i64=-2}, -2, 8, FLAGS, "device" },
    { "instances",  "Number of extra model instances to use on device",  DEVICE_OFFSET(extraThreadCount),  AV_OPT_TYPE_INT, {.i64=0}, 0, 3, FLAGS, "instances" },
    { "download",  "Enable model downloading",  BASIC_OFFSET(canDownloadModel),  AV_OPT_TYPE_INT, {.i64=1}, 0, 1, FLAGS, "canDownloadModels" },
    { "vram", "Max memory usage", DEVICE_OFFSET(maxMemory), AV_OPT_TYPE_DOUBLE, {.dbl=1.0}, 0.1, 1, .flags = FLAGS, "vram"},
    { "slowmo",  "Slowmo factor of the input video",  OFFSET(slowmo),  AV_OPT_TYPE_DOUBLE, {.dbl=1.0}, 0.1, 16, FLAGS, "slowmo" },
    { "rdt",  "Replace duplicate threshold. (0 or below means do not remove, high value will detect more duplicates)",  OFFSET(rdt),  AV_OPT_TYPE_DOUBLE, {.dbl=0.01}, -0.01, 0.2, FLAGS, "rdt" },
    { "fps", "output's frame rate, same as input frame rate if value is invalid", OFFSET(frame_rate), AV_OPT_TYPE_VIDEO_RATE, {.str = "0"}, 0, INT_MAX, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(tvai_fi);

static av_cold int init(AVFilterContext *ctx) {
    TVAIFIContext *tvai = ctx->priv;
    av_log(ctx, AV_LOG_DEBUG, "Init with params: %s %d %d %lf %d/%d = %lf\n", tvai->model, tvai->device, tvai->extraThreads, tvai->slowmo, tvai->frame_rate.num, tvai->frame_rate.den, av_q2d(tvai->frame_rate));
    tvai->previousFrame = NULL;
    return 0;
}

static int config_props(AVFilterLink *outlink) {
    AVFilterContext *ctx = outlink->src;
    TVAIFIContext *tvai = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    float threshold = 0.05;
    float fpsFactor = 0;
    tvai->basicInfo.scale = 1;
    if(tvai->frame_rate.num > 0) {
        AVRational frFactor = av_div_q(tvai->frame_rate, inlink->frame_rate);
        fpsFactor = 1/(tvai->slowmo*av_q2d(frFactor));

    } else {
        outlink->frame_rate = inlink->frame_rate;
        fpsFactor = 1/tvai->slowmo;
    }
    av_log(ctx, AV_LOG_ERROR, "MEESSSS download: %d index: %d threads: %d vram: %f model: %s scale: %d\n\n\n", tvai->basicInfo.canDownloadModel, tvai->basicInfo.device.index, tvai->basicInfo.device.extraThreadCount, 
    tvai->basicInfo.device.maxMemory, tvai->basicInfo.modelName, tvai->basicInfo.scale);
    av_log(ctx, AV_LOG_DEBUG, "Set time base to %d/%d %lf -> %d/%d %lf\n", inlink->time_base.num, inlink->time_base.den, av_q2d(inlink->time_base), outlink->time_base.num, outlink->time_base.den, av_q2d(outlink->time_base));
    av_log(ctx, AV_LOG_DEBUG, "Set frame rate to %lf -> %lf\n", av_q2d(inlink->frame_rate), av_q2d(outlink->frame_rate));
    av_log(ctx, AV_LOG_DEBUG, "Set fpsFactor to %lf generating %lf frames\n", fpsFactor, 1/fpsFactor);
    threshold = fpsFactor*0.3;
    float params[4] = {threshold, fpsFactor, tvai->slowmo, tvai->rdt};
    tvai->pFrameProcessor = ff_tvai_verifyAndCreate(inlink, outlink, 0, tvai->model, ModelTypeFrameInterpolation, tvai->device, tvai->extraThreads, tvai->vram, 1, tvai->canDownloadModels, params, 4, ctx);
    outlink->frame_rate = tvai->frame_rate.num > 0 ? tvai->frame_rate : inlink->frame_rate;
    outlink->time_base  = av_inv_q(outlink->frame_rate);
    return tvai->pFrameProcessor == NULL ? AVERROR(EINVAL) : 0;
}

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_RGB48,
    AV_PIX_FMT_RGB32,
    AV_PIX_FMT_NONE
};

static int filter_frame(AVFilterLink *inlink, AVFrame *in) {
    AVFilterContext *ctx = inlink->dst;
    TVAIFIContext *tvai = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    int ret = 0;
    if(ff_tvai_process(tvai->pFrameProcessor, in, 0)) {
        av_log(NULL, AV_LOG_ERROR, "The processing has failed\n");
        av_frame_free(&in);
        return AVERROR(ENOSYS);
    }
    if(tvai->previousFrame)
        av_frame_free(&tvai->previousFrame);
    tvai->previousFrame = in;
    ret = ff_tvai_add_output(tvai->pFrameProcessor, outlink, in, 0);
    return ret;
}

static int request_frame(AVFilterLink *outlink) {
    AVFilterContext *ctx = outlink->src;
    int ret = ff_request_frame(ctx->inputs[0]);
    if (ret == AVERROR_EOF) {
        TVAIFIContext *tvai = ctx->priv;
        int r = ff_tvai_postflight(outlink, tvai->pFrameProcessor, tvai->previousFrame);
        if(r)
            return r;
    }
    return ret;
}

static av_cold void uninit(AVFilterContext *ctx) {
    // TVAIFIContext *tvai = ctx->priv;
    // if(tvai->pFrameProcessor)
    //   tvai_destroy(tvai->pFrameProcessor);
}

static const AVFilterPad tvai_fi_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
};

static const AVFilterPad tvai_fi_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = config_props,
        .request_frame = request_frame,
    },
};

const AVFilter ff_vf_tvai_fi = {
    .name          = "tvai_fi",
    .description   = NULL_IF_CONFIG_SMALL("Apply Topaz Video AI frame interpolation models."),
    .priv_size     = sizeof(TVAIFIContext),
    .init          = init,
    .uninit        = uninit,
    FILTER_INPUTS(tvai_fi_inputs),
    FILTER_OUTPUTS(tvai_fi_outputs),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .priv_class    = &tvai_fi_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
