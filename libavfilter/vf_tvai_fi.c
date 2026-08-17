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
#include "avfilter_internal.h"
#include "video.h"
#include "tvai_common.h"

typedef struct  {
    const AVClass *class;
    BasicProcessorInfo basicInfo;
    double slowmo;
    double rdt;
    int timebaseUpdated;
    void* pFrameProcessor;
    AVRational frame_rate;
    AVFrame* previousFrame;
    AVDictionary *parameters;
    DictionaryItem *pModelParameters;
    int modelParametersCount;
    char *deviceString;    
} TVAIFIContext;

#define OFFSET(x) offsetof(TVAIFIContext, x)
#define BASIC_OFFSET(x) OFFSET(basicInfo) + offsetof(BasicProcessorInfo, x)
#define DEVICE_OFFSET(x) BASIC_OFFSET(device) + offsetof(DeviceSetting, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption tvai_fi_options[] = {
    { "model", "Model short name", BASIC_OFFSET(modelName), AV_OPT_TYPE_STRING, {.str="chr-2"}, .flags = FLAGS },
    { "device",  "Device index (Auto: -2, CPU: -1, GPU0: 0, ... or a . separated list of GPU indices e.g. 0.1.3)",  OFFSET(deviceString),  AV_OPT_TYPE_STRING, {.str="-2"}, .flags = FLAGS, "device" },
    { "instances",  "Number of extra model instances to use on device",  DEVICE_OFFSET(extraThreadCount),  AV_OPT_TYPE_INT, {.i64=0}, 0, 3, FLAGS, "instances" },
    { "download",  "Enable model downloading",  BASIC_OFFSET(canDownloadModel),  AV_OPT_TYPE_INT, {.i64=1}, 0, 1, FLAGS, "canDownloadModels" },
    { "vram", "Max memory usage", DEVICE_OFFSET(maxMemory), AV_OPT_TYPE_DOUBLE, {.dbl=1.0}, 0.1, 1, .flags = FLAGS, "vram"},
    { "slowmo",  "Slowmo factor of the input video",  OFFSET(slowmo),  AV_OPT_TYPE_DOUBLE, {.dbl=1.0}, 0.1, 16, FLAGS, "slowmo" },
    { "rdt",  "Replace duplicate threshold. (0 or below means do not remove, high value will detect more duplicates)",  OFFSET(rdt),  AV_OPT_TYPE_DOUBLE, {.dbl=0.01}, -0.01, 0.2, FLAGS, "rdt" },
    { "fps", "output's frame rate, same as input frame rate if value is invalid", OFFSET(frame_rate), AV_OPT_TYPE_VIDEO_RATE, {.str = "0"}, 0, INT_MAX, FLAGS },
    { "parameters", TVAI_FRAME_INTERPOLATION_PARAMETER_MESSAGE, OFFSET(parameters), AV_OPT_TYPE_DICT, {.str=""}, .flags = FLAGS, "parameters" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(tvai_fi);

static av_cold int init(AVFilterContext *ctx) {
    TVAIFIContext *tvai = ctx->priv;
    av_log(ctx, AV_LOG_DEBUG, "Init with params: %s %d %d %lf %d/%d = %lf\n", tvai->basicInfo.modelName, tvai->basicInfo.device.index, tvai->basicInfo.device.extraThreadCount, tvai->slowmo, tvai->frame_rate.num, tvai->frame_rate.den, av_q2d(tvai->frame_rate));
    tvai->previousFrame = NULL;
    return 0;
}

static int config_props(AVFilterLink *outlink) {
    AVFilterContext *ctx = outlink->src;
    TVAIFIContext *tvai = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    FilterLink *fInlink = ff_filter_link(inlink);
    FilterLink *fOutlink = ff_filter_link(outlink);
    float threshold = 0.05;
    float fpsFactor = 0;
    tvai->basicInfo.scale = 1;
    if(tvai->frame_rate.num > 0) {
        AVRational frFactor = av_div_q(tvai->frame_rate, fInlink->frame_rate);
        fpsFactor = 1/(tvai->slowmo*av_q2d(frFactor));
    } else {
        fOutlink->frame_rate = fInlink->frame_rate;
        fpsFactor = 1/tvai->slowmo;
    }
    VideoProcessorInfo info;
    threshold = fpsFactor*0.3;
    av_dict_set_float(&tvai->parameters, "threshold", threshold, 0);
    av_dict_set_float(&tvai->parameters, "fpsFactor", fpsFactor, 0);
    av_dict_set_float(&tvai->parameters, "slowmo", tvai->slowmo, 0);
    av_dict_set_float(&tvai->parameters, "rdt", tvai->rdt, 0);
    tvai->pModelParameters = ff_tvai_alloc_copy_entries(tvai->parameters, &tvai->modelParametersCount);
    ff_av_dict_log(ctx, "Parameters", tvai->parameters);
    if(ff_tvai_prepareProcessorInfo(tvai->deviceString, &info, ModelTypeFrameInterpolation, outlink, &(tvai->basicInfo), 0, tvai->pModelParameters, tvai->modelParametersCount)) {
        return AVERROR(EINVAL);
    }
    tvai->previousFrame = NULL;
    tvai->timebaseUpdated = tvai->frame_rate.num > 0 && av_q2d(av_inv_q(tvai->frame_rate)) < av_q2d(outlink->time_base);
    if(tvai->frame_rate.num > 0) {
        fOutlink->frame_rate = tvai->frame_rate;
    }
    info.basic.framerate = av_q2d(fOutlink->frame_rate);
    if(tvai->timebaseUpdated) {
        outlink->time_base  = av_inv_q(fOutlink->frame_rate);
        outlink->time_base = av_gcd_q(outlink->time_base, inlink->time_base, INT_MAX, outlink->time_base);
    }
    info.basic.timebase = av_q2d(outlink->time_base);

    tvai->pFrameProcessor = tvai_create(&info);
    av_log(ctx, AV_LOG_DEBUG, "Set time base to %d/%d %lf -> %d/%d %lf\n", inlink->time_base.num, inlink->time_base.den, av_q2d(inlink->time_base), outlink->time_base.num, outlink->time_base.den, av_q2d(outlink->time_base));
    av_log(ctx, AV_LOG_DEBUG, "Set frame rate to %lf -> %lf\n", av_q2d(fInlink->frame_rate), av_q2d(fOutlink->frame_rate));
    av_log(ctx, AV_LOG_DEBUG, "Set fpsFactor to %lf generating %lf frames\n", fpsFactor, 1/fpsFactor);
    return tvai->pFrameProcessor == NULL ? AVERROR(EINVAL) : 0;
}

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_RGB48,
    AV_PIX_FMT_NONE
};

static int filter_frame(AVFilterLink *inlink, AVFrame *in) {
    AVFilterContext *ctx = inlink->dst;
    TVAIFIContext *tvai = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    TVAIBuffer iBuffer;
    iBuffer.pBuffer = in->data[0];
    iBuffer.lineSize = in->linesize[0];
    if(tvai->timebaseUpdated) {
        iBuffer.pts = av_rescale_q_rnd(in->pts, inlink->time_base, outlink->time_base, AV_ROUND_PASS_MINMAX);
        iBuffer.duration = av_rescale_q_rnd(in->duration, inlink->time_base, outlink->time_base, AV_ROUND_PASS_MINMAX);
    } else {
        iBuffer.pts = in->pts;
        iBuffer.duration = in->duration;
    }
    if(tvai->pFrameProcessor == NULL || tvai_process(tvai->pFrameProcessor, &iBuffer)) {
        av_log(NULL, AV_LOG_ERROR, "The processing has failed\n");
        av_frame_free(&in);
        return AVERROR(ENOSYS);
    }
    if(tvai->previousFrame)
        av_frame_free(&tvai->previousFrame);
    tvai->previousFrame = in;
    return ff_tvai_add_output(tvai->pFrameProcessor, outlink, in);
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
    TVAIFIContext *tvai = ctx->priv;
    if(tvai->pFrameProcessor)
      tvai_destroy(tvai->pFrameProcessor);
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

const FFFilter ff_vf_tvai_fi = {
    .p.name          = "tvai_fi",
    .p.description   = NULL_IF_CONFIG_SMALL("Apply Topaz Video AI frame interpolation models."),
    .priv_size     = sizeof(TVAIFIContext),
    .init          = init,
    .uninit        = uninit,
    FILTER_INPUTS(tvai_fi_inputs),
    FILTER_OUTPUTS(tvai_fi_outputs),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .p.priv_class    = &tvai_fi_class,
    .p.flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
