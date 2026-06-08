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
 * Topaz Video AI Stabilization filter
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
#include "tvai.h"
#include "tvai_common.h"
#include "tvai_messages.h"
#include "float.h"

typedef struct TVAIStbContext {
    const AVClass *class;
    BasicProcessorInfo basicInfo;
    char *filename;
    void* pFrameProcessor;
    double smoothness;
    int windowSize, cacheSize, stabDOF, enableRSC, enableFullFrame, reduceMotion;
    double readStartTime, readEndTime, canvasScaleX, canvasScaleY;
    AVFrame* previousFrame;
    AVDictionary *parameters;
    DictionaryItem *pModelParameters;
    int modelParametersCount;
    char *deviceString;    
} TVAIStbContext;

#define OFFSET(x) offsetof(TVAIStbContext, x)
#define BASIC_OFFSET(x) OFFSET(basicInfo) + offsetof(BasicProcessorInfo, x)
#define DEVICE_OFFSET(x) BASIC_OFFSET(device) + offsetof(DeviceSetting, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption tvai_stb_options[] = {
    { "model", "Model short name", BASIC_OFFSET(modelName), AV_OPT_TYPE_STRING, {.str="ref-2"}, .flags = FLAGS },
    { "device",  "Device index (Auto: -2, CPU: -1, GPU0: 0, ... or a . separated list of GPU indices e.g. 0.1.3)",  OFFSET(deviceString),  AV_OPT_TYPE_STRING, {.str="-2"}, .flags = FLAGS, "device" },
    { "instances",  "Number of extra model instances to use on device",  DEVICE_OFFSET(extraThreadCount),  AV_OPT_TYPE_INT, {.i64=0}, 0, 3, FLAGS, "instances" },
    { "download",  "Enable model downloading",  BASIC_OFFSET(canDownloadModel),  AV_OPT_TYPE_INT, {.i64=1}, 0, 1, FLAGS, "canDownloadModels" },
    { "vram", "Max memory usage", DEVICE_OFFSET(maxMemory), AV_OPT_TYPE_DOUBLE, {.dbl=1.0}, 0.1, 1, .flags = FLAGS, "vram"},
    { "full", "Perform full-frame stabilization. If disabled, performs auto-crop (ignores full-reame related options)", OFFSET(enableFullFrame), AV_OPT_TYPE_INT, {.i64=1}, 0, 1, .flags = FLAGS, "full" },
    { "filename", "CPE output filename", OFFSET(filename), AV_OPT_TYPE_STRING, {.str="cpe.json"}, .flags = FLAGS, "filename"},
    { "rst", "Read start time relative to CPE", OFFSET(readStartTime), AV_OPT_TYPE_DOUBLE, {.dbl=0}, 0, DBL_MAX, .flags = FLAGS, "rst" },
    { "ret", "Read end time relative to read start time (rst)", OFFSET(readEndTime), AV_OPT_TYPE_DOUBLE, {.dbl=DBL_MAX}, 0, DBL_MAX, .flags = FLAGS, "ret" },
    { "ws", "Window size for full-frame synthesis", OFFSET(windowSize), AV_OPT_TYPE_INT, {.i64=64}, 0, 512, .flags = FLAGS, "ws"  },
    { "csx", "Scale of the canvas relative to input width", OFFSET(canvasScaleX), AV_OPT_TYPE_DOUBLE, {.dbl=2}, 1, 8, .flags = FLAGS, "csx"  },
    { "csy", "Scale of the canvas relative to input height", OFFSET(canvasScaleY), AV_OPT_TYPE_DOUBLE, {.dbl=2}, 1, 8, .flags = FLAGS, "csy"  },
    { "smoothness", "Amount of smoothness to be applied on the camera trajectory to stabilize the video",  OFFSET(smoothness),  AV_OPT_TYPE_DOUBLE, {.dbl=6.0}, 0.0, 16.0, FLAGS, "smoothness" },
    { "cache", "Set memory cache size", OFFSET(cacheSize), AV_OPT_TYPE_INT, {.i64=128}, 0, 256, .flags = FLAGS, "cache" },
    { "dof", "Enable/Disable stabilization of different motions - rotation (1st digit), horizontal pan (2nd), vertical pan (3rd), scale/zoom (4th digit). Non-zero digit enables corresponding motions", OFFSET(stabDOF), AV_OPT_TYPE_INT, {.i64=1111}, 0, 1111, .flags = FLAGS, "dof" },
    { "roll", "Enable rolling shutter correction", OFFSET(enableRSC), AV_OPT_TYPE_INT, {.i64=0}, 0, 1, .flags = FLAGS, "roll" },
    { "reduce", "Reduce motion jitters", OFFSET(reduceMotion), AV_OPT_TYPE_INT, {.i64=0}, 0, 5, .flags = FLAGS, "reduce" },
    { "parameters", TVAI_STABILIZATION_PARAMETER_MESSAGE, OFFSET(parameters), AV_OPT_TYPE_DICT, {.str=""}, .flags = FLAGS, "parameters" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(tvai_stb);

static av_cold int init(AVFilterContext *ctx) {
  TVAIStbContext *tvai = ctx->priv;
  av_log(ctx, AV_LOG_VERBOSE, "Here init with params: %s %d %s %lf\n", tvai->basicInfo.modelName, tvai->basicInfo.device.index, tvai->filename, tvai->smoothness);
  tvai->previousFrame = NULL;
  return 0;
}

static int config_props(AVFilterLink *outlink) {
    AVFilterContext *ctx = outlink->src;
    TVAIStbContext *tvai = ctx->priv;
    VideoProcessorInfo info;
    tvai->basicInfo.scale = 1;
    av_dict_set(&tvai->parameters, "cpePath", tvai->filename, 0);
    av_dict_set_float(&tvai->parameters, "smoothness", tvai->smoothness, 0);
    av_dict_set_float(&tvai->parameters, "windowSize", tvai->windowSize, 0);
    av_dict_set_float(&tvai->parameters, "canvasScaleX", tvai->canvasScaleX, 0);
    av_dict_set_float(&tvai->parameters, "canvasScaleY", tvai->canvasScaleY, 0);
    av_dict_set_float(&tvai->parameters, "cacheAfter", tvai->cacheSize, 0);
    av_dict_set_float(&tvai->parameters, "stabDOF", tvai->stabDOF, 0);
    av_dict_set_float(&tvai->parameters, "enableRSC", tvai->enableRSC, 0);
    av_dict_set_float(&tvai->parameters, "readStartTime", tvai->readStartTime, 0);
    av_dict_set_float(&tvai->parameters, "readEndTime", tvai->readEndTime, 0);
    av_dict_set_float(&tvai->parameters, "reduceMotion", tvai->reduceMotion, 0);
    tvai->pModelParameters = ff_tvai_alloc_copy_entries(tvai->parameters, &tvai->modelParametersCount);
  
  if(ff_tvai_prepareProcessorInfo(tvai->deviceString, &info, ModelTypeStabilization, outlink, &(tvai->basicInfo), tvai->enableFullFrame > 0, tvai->pModelParameters, tvai->modelParametersCount)) {
    return AVERROR(EINVAL);  
  }
  tvai->pFrameProcessor = tvai_create(&info);
  if(tvai->pFrameProcessor == NULL) {
    return AVERROR(EINVAL);
  }
  if(!tvai->enableFullFrame) {
    tvai_stabilize_get_output_size(tvai->pFrameProcessor, &(outlink->w), &(outlink->h));
    av_log(NULL, AV_LOG_VERBOSE, "Auto-crop stabilization output size: %d x %d\n", outlink->w, outlink->h);
  }
  return 0;
}

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_BGR48,
    AV_PIX_FMT_NONE
};

static int filter_frame(AVFilterLink *inlink, AVFrame *in) {
    AVFilterContext *ctx = inlink->dst;
    TVAIStbContext *tvai = ctx->priv;
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
    TVAIStbContext *tvai = ctx->priv;
    int ret = ff_request_frame(ctx->inputs[0]);
    if (ret == AVERROR_EOF) {
        int r = ff_tvai_postflight(outlink, tvai->pFrameProcessor, tvai->previousFrame);
        if(r)
            return r;
    }
    return ret;
}

static av_cold void uninit(AVFilterContext *ctx) {
    TVAIStbContext *tvai = ctx->priv;
    av_log(ctx, AV_LOG_DEBUG, "Uninit called for %s %d\n", tvai->basicInfo.modelName, tvai->pFrameProcessor == NULL);
    if(tvai->pFrameProcessor)
        tvai_destroy(tvai->pFrameProcessor);
}

static const AVFilterPad tvai_stb_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
};

static const AVFilterPad tvai_stb_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = config_props,
        .request_frame = request_frame,
    },
};

const FFFilter ff_vf_tvai_stb = {
    .p.name          = "tvai_stb",
    .p.description   = NULL_IF_CONFIG_SMALL("Apply Topaz Video AI stabilization models"),
    .priv_size     = sizeof(TVAIStbContext),
    .init          = init,
    .uninit        = uninit,
    FILTER_INPUTS(tvai_stb_inputs),
    FILTER_OUTPUTS(tvai_stb_outputs),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .p.priv_class    = &tvai_stb_class,
    .p.flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
