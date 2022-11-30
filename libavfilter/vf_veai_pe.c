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
#include "veai_common.h"

typedef struct VEAIParamContext {
    const AVClass *class;
    char *model;
    int device;
    int canDownloadModels;
    void* pParamEstimator;
    int firstFrame;
} VEAIParamContext;

#define OFFSET(x) offsetof(VEAIParamContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption veai_pe_options[] = {
    { "model", "Model short name", OFFSET(model), AV_OPT_TYPE_STRING, {.str="prap-3"}, .flags = FLAGS },
    { "device",  "Device index (Auto: -2, CPU: -1, GPU0: 0, ...)",  OFFSET(device),  AV_OPT_TYPE_INT, {.i64=-2}, -2, 8, FLAGS, "device" },
    { "download",  "Enable model downloading",  OFFSET(canDownloadModels),  AV_OPT_TYPE_INT, {.i64=1}, 0, 1, FLAGS, "canDownloadModels" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(veai_pe);

static av_cold int init(AVFilterContext *ctx) {
  VEAIParamContext *veai = ctx->priv;
  av_log(NULL, AV_LOG_DEBUG, "Here init with params: %s %d\n", veai->model, veai->device);
  veai->firstFrame = 1;
  return veai->pParamEstimator == NULL;
}

static int config_props(AVFilterLink *outlink) {
    AVFilterContext *ctx = outlink->src;
    VEAIParamContext *veai = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];

    veai->pParamEstimator = ff_veai_verifyAndCreate(inlink, outlink, (char*)"pe", veai->model, ModelTypeParameterEstimation, veai->device, 0, 1, 1, veai->canDownloadModels, NULL, 0, ctx);
    return veai->pParamEstimator == NULL ? AVERROR(EINVAL) : 0;
    return 0;
}


static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_RGB48,
    AV_PIX_FMT_NONE
};

static int filter_frame(AVFilterLink *inlink, AVFrame *in) {
    AVFilterContext *ctx = inlink->dst;
    VEAIParamContext *veai = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    float parameters[VEAI_MAX_PARAMETER_COUNT] = {0};
    int result = ff_veai_estimateParam(ctx, veai->pParamEstimator, in, veai->firstFrame, parameters);
    if(!(result == 0 || result == 1)) {
        return result;
    }
    veai->firstFrame = 0;
    return ff_filter_frame(outlink, in);
}

static av_cold void uninit(AVFilterContext *ctx) {
    VEAIParamContext *veai = ctx->priv;
    veai_destroy(veai->pParamEstimator);
}

static const AVFilterPad veai_pe_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
};

static const AVFilterPad veai_pe_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = config_props,
    },
};

const AVFilter ff_vf_veai_pe = {
    .name          = "veai_pe",
    .description   = NULL_IF_CONFIG_SMALL("Apply Video Enhance AI models."),
    .priv_size     = sizeof(VEAIParamContext),
    .init          = init,
    .uninit        = uninit,
    FILTER_INPUTS(veai_pe_inputs),
    FILTER_OUTPUTS(veai_pe_outputs),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .priv_class    = &veai_pe_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
