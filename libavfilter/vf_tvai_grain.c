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
 * Topaz Video AI Grain filter
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

typedef struct TVAIGrainContext {
    const AVClass *class;
    BasicProcessorInfo basicInfo;
    double grain, grainSize;
    void* pFrameProcessor;
    AVFrame* previousFrame;
    AVDictionary *parameters;
    DictionaryItem* modelParameters;
    int modelParameterCount;
} TVAIGrainContext;

#define OFFSET(x) offsetof(TVAIGrainContext, x)
#define BASIC_OFFSET(x) OFFSET(basicInfo) + offsetof(BasicProcessorInfo, x)
#define DEVICE_OFFSET(x) BASIC_OFFSET(device) + offsetof(DeviceSetting, x)

#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption tvai_grain_options[] = {
    { "grain",  "The amount of grain to add to the output",  OFFSET(grain),  AV_OPT_TYPE_DOUBLE, {.dbl=0}, 0.0, 1.0, FLAGS, "grain" },
    { "download",  "Enable model downloading",  BASIC_OFFSET(canDownloadModel),  AV_OPT_TYPE_INT, {.i64=1}, 0, 1, FLAGS, "canDownloadModels" },
    { "gsize",  "The size of grain to be added",  OFFSET(grainSize),  AV_OPT_TYPE_DOUBLE, {.dbl=0}, 0.0, 5.0, FLAGS, "gsize" },
    { "parameters", TVAI_GRAIN_PARAMETER_MESSAGE, OFFSET(parameters), AV_OPT_TYPE_DICT, {.str=""}, .flags = FLAGS, "parameters" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(tvai_grain);

static av_cold int init(AVFilterContext *ctx) {
  TVAIGrainContext *tvai = ctx->priv;
  tvai->previousFrame = NULL;
  return 0;
}

static int config_props(AVFilterLink *outlink) {
    AVFilterContext *ctx = outlink->src;
    TVAIGrainContext *tvai = ctx->priv;
    av_dict_set_float(&tvai->parameters, "grain", tvai->grain, 0);
    av_dict_set_float(&tvai->parameters, "grainSize", tvai->grainSize, 0);
    
    tvai->modelParameters = ff_tvai_alloc_copy_entries(tvai->parameters, &(tvai->modelParameterCount));
    VideoProcessorInfo info;
    ff_av_dict_log(ctx, "Parameters", tvai->parameters);
    strcpy(info.basic.processorName, "grain");
    info.basic.modelName = "grain";

    const char* model_dir = getenv("TVAI_MODEL_DIR");
    const char* model_data_dir = getenv("TVAI_MODEL_DATA_DIR");
    if (model_dir == NULL) model_dir = "";
    if (model_data_dir == NULL) model_data_dir = "";
    
    if (!tvai_setup_model_manager((char*)model_dir, (char*)model_data_dir, tvai->basicInfo.canDownloadModel)) {
        av_log(NULL, AV_LOG_ERROR, "Failed to setup model manager\n");
        return AVERROR(ENOSYS);
    }

    info.basic.preflight = 0;
    AVFilterLink* pInlink = ctx->inputs[0];
    FilterLink *fInlink = ff_filter_link(pInlink);
    FilterLink *fOutlink = ff_filter_link(outlink);
    
    switch (pInlink->format) {
    case AV_PIX_FMT_RGBF32:
        info.basic.pixelFormat = TVAIPixelFormatRGB32F;
        break;
    case AV_PIX_FMT_RGBAF32:
        info.basic.pixelFormat = TVAIPixelFormatRGBA32F;
        break;
    default:
        info.basic.pixelFormat = TVAIPixelFormatRGB16;
        break;
    }

    info.basic.inputWidth = pInlink->w;
    info.basic.inputHeight = pInlink->h;
    info.basic.scale = 1;
    info.basic.preflight = 0;
    info.basic.timebase = av_q2d(pInlink->time_base);
    info.basic.framerate = av_q2d(fInlink->frame_rate);
    info.outputWidth = outlink->w = pInlink->w*info.basic.scale;
    info.outputHeight = outlink->h = pInlink->h*info.basic.scale;
    info.basic.pParameters = tvai->modelParameters;
    info.basic.parameterCount = tvai->modelParameterCount;
    outlink->time_base = pInlink->time_base;
    fOutlink->frame_rate = fInlink->frame_rate;
    outlink->sample_aspect_ratio = pInlink->sample_aspect_ratio;
    
    tvai->pFrameProcessor = tvai_create(&info);
    tvai->previousFrame = NULL;
    return tvai->pFrameProcessor == NULL ? AVERROR(EINVAL) : 0;
}

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_RGB48,
    AV_PIX_FMT_RGBF32,
    AV_PIX_FMT_RGBAF32,
    AV_PIX_FMT_NONE
};

static int filter_frame(AVFilterLink *inlink, AVFrame *in) {
    AVFilterContext *ctx = inlink->dst;
    TVAIGrainContext *tvai = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    if(ff_tvai_process(tvai->pFrameProcessor, in)) {
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
    TVAIGrainContext *tvai = ctx->priv;
    int ret = ff_request_frame(ctx->inputs[0]);
    if (ret == AVERROR_EOF) {
        int r = ff_tvai_postflight(outlink, tvai->pFrameProcessor, tvai->previousFrame);
        if(r)
            return r;
    }
    return ret;
}

static av_cold void uninit(AVFilterContext *ctx) {
    TVAIGrainContext *tvai = ctx->priv;
    av_log(ctx, AV_LOG_DEBUG, "Uninit called for %s %d\n", tvai->basicInfo.modelName, tvai->pFrameProcessor == NULL);
    if(tvai->pFrameProcessor)
        tvai_destroy(tvai->pFrameProcessor);
}

static const AVFilterPad tvai_grain_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
};

static const AVFilterPad tvai_grain_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = config_props,
        .request_frame = request_frame,
    },
};

const FFFilter ff_vf_tvai_grain = {
    .p.name          = "tvai_grain",
    .p.description   = NULL_IF_CONFIG_SMALL("Apply Topaz Video AI grain models, parameters will only be applied to appropriate models"),
    .priv_size     = sizeof(TVAIGrainContext),
    .init          = init,
    .uninit        = uninit,
    FILTER_INPUTS(tvai_grain_inputs),
    FILTER_OUTPUTS(tvai_grain_outputs),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .p.priv_class    = &tvai_grain_class,
    .p.flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
