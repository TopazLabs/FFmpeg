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
    BasicProcessorInfo basicInfo;
    char *filename;
    void *pFrameProcessor;
} TVAICPEContext;

#define OFFSET(x) offsetof(TVAICPEContext, x)
#define BASIC_OFFSET(x) OFFSET(basicInfo) + offsetof(BasicProcessorInfo, x)
#define DEVICE_OFFSET(x) BASIC_OFFSET(device) + offsetof(DeviceSetting, x)

#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption tvai_cpe_options[] = {
    { "model", "Model short name", BASIC_OFFSET(modelName), AV_OPT_TYPE_STRING, {.str="cpe-1"}, .flags = FLAGS },
    { "filename", "CPE output filename", OFFSET(filename), AV_OPT_TYPE_STRING, {.str="cpe.json"}, .flags = FLAGS },
    { "device",  "Device index (Auto: -2, CPU: -1, GPU0: 0, ...)",  DEVICE_OFFSET(index),  AV_OPT_TYPE_INT, {.i64=-2}, -2, 8, FLAGS, "device" },
    { "download",  "Enable model downloading",  BASIC_OFFSET(canDownloadModel),  AV_OPT_TYPE_INT, {.i64=1}, 0, 1, FLAGS, "canDownloadModels" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(tvai_cpe);

static av_cold int init(AVFilterContext *ctx) {
  TVAICPEContext *tvai = ctx->priv;
  av_log(ctx, AV_LOG_DEBUG, "Here init with params: %s %d\n", tvai->basicInfo.modelName, tvai->basicInfo.device.index);
  return 0;
}

static int config_props(AVFilterLink *outlink) {
    AVFilterContext *ctx = outlink->src;
    TVAICPEContext *tvai = ctx->priv;
    VideoProcessorInfo info;
    tvai->basicInfo.scale = 1;
    tvai->basicInfo.device.maxMemory = 1;
    tvai->basicInfo.device.extraThreadCount = 0;
    info.options[0] = tvai->filename;
    float rsc = strncmp(tvai->basicInfo.modelName, (char*)"cpe-1", 5) != 0;
    av_log(ctx, AV_LOG_ERROR, "Model: %s RSC: %f\n", tvai->basicInfo.modelName, rsc);
    if(ff_tvai_prepareProcessorInfo(&info, ModelTypeCamPoseEstimation, outlink, &(tvai->basicInfo), 0, &rsc, 1)) {
      return AVERROR(EINVAL);  
    }
    av_log(ctx, AV_LOG_ERROR, "File: %s Model: %s RSC: %f\n", info.options[0], info.basic.modelName, rsc);
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
    if(ff_tvai_process(tvai->pFrameProcessor, in)) {
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
        av_log(ctx, AV_LOG_DEBUG, "End of file reached %s %d\n", tvai->basicInfo.modelName, tvai->pFrameProcessor == NULL);
    }
    return ret;
}

static av_cold void uninit(AVFilterContext *ctx) {
    TVAICPEContext *tvai = ctx->priv;
    av_log(ctx, AV_LOG_DEBUG, "Uninit called for %s\n", tvai->basicInfo.modelName);
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
