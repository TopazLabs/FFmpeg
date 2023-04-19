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
 * Topaz Video AI camera pose estimation filter
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
#include "tvai.h"
#include "tvai_common.h"

typedef struct TVAICPEContext {
    const AVClass *class;
    char *model, *filename;
    int device;
    int canDownloadModels;
    void* pFrameProcessor;
    unsigned int counter;
    int rsc;
} TVAICPEContext;

#define OFFSET(x) offsetof(TVAICPEContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption tvai_cpe_options[] = {
    { "model", "Model short name", OFFSET(model), AV_OPT_TYPE_STRING, {.str="cpe-1"}, .flags = FLAGS },
    { "filename", "CPE output filename", OFFSET(filename), AV_OPT_TYPE_STRING, {.str="cpe.json"}, .flags = FLAGS },
    { "device",  "Device index (Auto: -2, CPU: -1, GPU0: 0, ...)",  OFFSET(device),  AV_OPT_TYPE_INT, {.i64=-2}, -2, 8, FLAGS, "device" },
    { "download",  "Enable model downloading",  OFFSET(canDownloadModels),  AV_OPT_TYPE_INT, {.i64=1}, 0, 1, FLAGS, "canDownloadModels" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(tvai_cpe);

static av_cold int init(AVFilterContext *ctx) {
  TVAICPEContext *tvai = ctx->priv;
  av_log(ctx, AV_LOG_DEBUG, "Here init with params: %s %d\n", tvai->model, tvai->device);
  tvai->counter = 0;
  return 0;
}

static int config_props(AVFilterLink *outlink) {
    AVFilterContext *ctx = outlink->src;
    TVAICPEContext *tvai = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    VideoProcessorInfo info;
    info.options[0] = tvai->filename;
    tvai->rsc = strncmp(tvai->model, (char*)"cpe-1", 5) != 0;
    av_log(ctx, AV_LOG_DEBUG, "RSC: %d\n", tvai->rsc);
    if(ff_tvai_verifyAndSetInfo(&info, inlink, outlink, 0, tvai->model, ModelTypeCamPoseEstimation, tvai->device, 0, 1, 1, tvai->canDownloadModels, &tvai->rsc, 1, ctx)) {
      return AVERROR(EINVAL);
    }
    tvai->pFrameProcessor = tvai_create(&info);
    return tvai->pFrameProcessor == NULL ? AVERROR(EINVAL) : 0;
}

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_BGR48,
    AV_PIX_FMT_NONE
};

static int filter_frame(AVFilterLink *inlink, AVFrame *in) {
    AVFilterContext *ctx = inlink->dst;
    TVAICPEContext *tvai = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    int ret = 0;
    if(ff_tvai_process(tvai->pFrameProcessor, in, 0)) {
        av_log(NULL, AV_LOG_ERROR, "The processing has failed\n");
        av_frame_free(&in);
        return AVERROR(ENOSYS);
    }
    ff_tvai_ignore_output(tvai->pFrameProcessor);
    return ff_filter_frame(outlink, in);
}

static int request_frame(AVFilterLink *outlink) {
    AVFilterContext *ctx = outlink->src;
    TVAICPEContext *tvai = ctx->priv;
    int ret = ff_request_frame(ctx->inputs[0]);
    if (ret == AVERROR_EOF) {
        tvai_end_stream(tvai->pFrameProcessor);
        while(tvai_remaining_frames(tvai->pFrameProcessor) > 0) {
            ff_tvai_ignore_output(tvai->pFrameProcessor);
            tvai_wait(20);
        }
        av_log(ctx, AV_LOG_DEBUG, "End of file reached %s %d\n", tvai->model, tvai->pFrameProcessor == NULL);
    }
    return ret;
}

static av_cold void uninit(AVFilterContext *ctx) {
    TVAICPEContext *tvai = ctx->priv;
    av_log(ctx, AV_LOG_DEBUG, "Uninit called for %s\n", tvai->model);
    // if(tvai->pFrameProcessor)
    //     tvai_destroy(tvai->pFrameProcessor);
}

static const AVFilterPad tvai_cpe_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
};

static const AVFilterPad tvai_cpe_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = config_props,
        .request_frame = request_frame,
    },
};

const AVFilter ff_vf_tvai_cpe = {
    .name          = "tvai_cpe",
    .description   = NULL_IF_CONFIG_SMALL("Apply Topaz Video AI camera pose estimation model."),
    .priv_size     = sizeof(TVAICPEContext),
    .init          = init,
    .uninit        = uninit,
    FILTER_INPUTS(tvai_cpe_inputs),
    FILTER_OUTPUTS(tvai_cpe_outputs),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .priv_class    = &tvai_cpe_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
