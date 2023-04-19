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
 * Video Enhance AI Parameter Estimation filter
 *
 * @see https://www.topazlabs.com/topaz-video-ai
 */

#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/avutil.h"
#include "tvai_common.h"

typedef struct TVAIParamContext {
    const AVClass *class;
    char *model;
    int device;
    int canDownloadModels;
    void* pParamEstimator;
    int firstFrame;
} TVAIParamContext;

#define OFFSET(x) offsetof(TVAIParamContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption tvai_pe_options[] = {
    { "model", "Model short name", OFFSET(model), AV_OPT_TYPE_STRING, {.str="prap-3"}, .flags = FLAGS },
    { "device",  "Device index (Auto: -2, CPU: -1, GPU0: 0, ...)",  OFFSET(device),  AV_OPT_TYPE_INT, {.i64=-2}, -2, 8, FLAGS, "device" },
    { "download",  "Enable model downloading",  OFFSET(canDownloadModels),  AV_OPT_TYPE_INT, {.i64=1}, 0, 1, FLAGS, "canDownloadModels" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(tvai_pe);

static av_cold int init(AVFilterContext *ctx) {
  TVAIParamContext *tvai = ctx->priv;
  av_log(NULL, AV_LOG_DEBUG, "Here init with params: %s %d\n", tvai->model, tvai->device);
  tvai->firstFrame = 1;
  return tvai->pParamEstimator == NULL;
}

static int config_props(AVFilterLink *outlink) {
    AVFilterContext *ctx = outlink->src;
    TVAIParamContext *tvai = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];

    tvai->pParamEstimator = ff_tvai_verifyAndCreate(inlink, outlink, 0, tvai->model, ModelTypeParameterEstimation, tvai->device, 0, 1, 1, tvai->canDownloadModels, NULL, 0, ctx);
    return tvai->pParamEstimator == NULL ? AVERROR(EINVAL) : 0;
    return 0;
}


static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_RGB48,
    AV_PIX_FMT_NONE
};

static int filter_frame(AVFilterLink *inlink, AVFrame *in) {
    AVFilterContext *ctx = inlink->dst;
    TVAIParamContext *tvai = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    int ret = 0;
    if(ff_tvai_process(tvai->pParamEstimator, in, 0)) {
        av_log(NULL, AV_LOG_ERROR, "The processing has failed\n");
        av_frame_free(&in);
        return AVERROR(ENOSYS);
    }
    return ff_filter_frame(outlink, in);
}

static int request_frame(AVFilterLink *outlink) {
    AVFilterContext *ctx = outlink->src;
    TVAIParamContext *tvai = ctx->priv;
    int ret = ff_request_frame(ctx->inputs[0]);
    if (ret == AVERROR_EOF) {
        tvai_end_stream(tvai->pParamEstimator);
        av_log(ctx, AV_LOG_DEBUG, "End of file reached %s %d\n", tvai->model, tvai->pParamEstimator == NULL);
    }
    return ret;
}

static av_cold void uninit(AVFilterContext *ctx) {
    TVAIParamContext *tvai = ctx->priv;
    tvai_destroy(tvai->pParamEstimator);
}

static const AVFilterPad tvai_pe_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
};

static const AVFilterPad tvai_pe_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = config_props,
    },
};

const AVFilter ff_vf_tvai_pe = {
    .name          = "tvai_pe",
    .description   = NULL_IF_CONFIG_SMALL("Apply Topaz Video AI parameter estimation models."),
    .priv_size     = sizeof(TVAIParamContext),
    .init          = init,
    .uninit        = uninit,
    FILTER_INPUTS(tvai_pe_inputs),
    FILTER_OUTPUTS(tvai_pe_outputs),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .priv_class    = &tvai_pe_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
