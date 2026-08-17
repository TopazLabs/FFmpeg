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
 * Topaz Video AI Upscale filter
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

typedef struct TVAIUpContext {
    const AVClass *class;
    BasicProcessorInfo basicInfo;
    int estimateFrameCount, count, estimating, w, h, canKeepColor;
    double preBlur, noise, details, halo, blur, compression;
    double prenoise, grain, grainSize, blend;
    void* pFrameProcessor;
    AVFrame* previousFrame;
    AVDictionary *parameters;
    DictionaryItem* modelParameters;
    int modelParameterCount;
    char *deviceString;    
} TVAIUpContext;

#define OFFSET(x) offsetof(TVAIUpContext, x)
#define BASIC_OFFSET(x) OFFSET(basicInfo) + offsetof(BasicProcessorInfo, x)
#define DEVICE_OFFSET(x) BASIC_OFFSET(device) + offsetof(DeviceSetting, x)

#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption tvai_up_options[] = {
    { "model", "Model short name", BASIC_OFFSET(modelName), AV_OPT_TYPE_STRING, {.str="ahq-12"}, .flags = FLAGS },
    { "scale",  "Output scale",  BASIC_OFFSET(scale),  AV_OPT_TYPE_INT, {.i64=1}, 0, 4, FLAGS, "scale" },
    { "w",  "Estimate scale based on output width",  OFFSET(w),  AV_OPT_TYPE_INT, {.i64=0}, 0, 100000, FLAGS, "w" },
    { "h",  "Estimate scale based on output height",  OFFSET(h),  AV_OPT_TYPE_INT, {.i64=0}, 0, 100000, FLAGS, "h" },
    { "device",  "Device index (Auto: -2, CPU: -1, GPU0: 0, ... or a . separated list of GPU indices e.g. 0.1.3)",  OFFSET(deviceString),  AV_OPT_TYPE_STRING, {.str="-2"}, .flags = FLAGS, "device" },
    { "instances",  "Number of extra model instances to use on device",  DEVICE_OFFSET(extraThreadCount),  AV_OPT_TYPE_INT, {.i64=0}, 0, 3, FLAGS, "instances" },
    { "download",  "Enable model downloading",  BASIC_OFFSET(canDownloadModel),  AV_OPT_TYPE_INT, {.i64=1}, 0, 1, FLAGS, "canDownloadModels" },
    { "vram", "Max memory usage", DEVICE_OFFSET(maxMemory), AV_OPT_TYPE_DOUBLE, {.dbl=1.0}, 0.1, 1, .flags = FLAGS, "vram"},
    { "estimate",  "Number of frames for auto parameter estimation, 0 to disable auto parameter estimation",  OFFSET(estimateFrameCount),  AV_OPT_TYPE_INT, {.i64=0}, 0, 100, FLAGS, "estimateParamNthFrame" },
    { "preblur",  "Adjusts both the antialiasing and deblurring strength relative to the amount of aliasing and blurring in the input video. \nNegative values are better if the input video has aliasing artifacts such as moire patterns or staircasing. Positive values are better if the input video has more lens blurring than aliasing artifacts. ",  OFFSET(preBlur),  AV_OPT_TYPE_DOUBLE, {.dbl=0}, -1.0, 1.0, FLAGS, "preblur" },
    { "noise",  "Removes ISO noise from the input video. Higher values remove more noise but may also remove fine details. \nNote that this value is relative to the amount of noise found in the input video - higher values on videos with low amounts of ISO noise may introduce more artifacts.",  OFFSET(noise),  AV_OPT_TYPE_DOUBLE, {.dbl=0}, -1.0, 1.0, FLAGS, "noise" },
    { "details",  "Used to recover fine texture and detail lost due to in-camera noise suppression. \nThis value is relative to the amount of noise suppression in the camera used for the input video, and higher values may introduce artifacts if the input video has little to no in-camera noise suppression.",  OFFSET(details),  AV_OPT_TYPE_DOUBLE, {.dbl=0}, -1.0, 1.0, FLAGS, "details" },
    { "halo",  "Increase this if the input video has halo or ring artifacts around strong edges caused by oversharpening. \nThis value is relative to the amount of haloing artifacts in the input video, and has a \"sweet spot\". Values that are too high for the input video may cause additional artifacts to appear.",  OFFSET(halo),  AV_OPT_TYPE_DOUBLE, {.dbl=0}, -1.0, 1.0, FLAGS, "halo" },
    { "blur",  "Additional sharpening of the video. Use this if the input video looks too soft. \nThe value set should be relative to the amount of softness in the input video - if the input video is already sharp, higher values will introduce more artifacts.",  OFFSET(blur),  AV_OPT_TYPE_DOUBLE, {.dbl=0}, -1.0, 1.0, FLAGS, "blur" },
    { "compression",  "Reduces compression artifacts from codec encoding, such as blockiness or mosquito noise. Higher values are best for low bitrate videos.\nNote that the value should be relative to the amount of compression artifacts in the input video - higher values on a video with few compression artifacts will introduce more artifacts into the output.",  OFFSET(compression),  AV_OPT_TYPE_DOUBLE, {.dbl=0}, -1.0, 1.0, FLAGS, "compression" },
    { "prenoise",  "The amount of noise to add to the input before processing",  OFFSET(prenoise),  AV_OPT_TYPE_DOUBLE, {.dbl=0}, 0.0, 0.1, FLAGS, "prenoise" },
    { "grain",  "The amount of grain to add to the output",  OFFSET(grain),  AV_OPT_TYPE_DOUBLE, {.dbl=0}, 0.0, 1.0, FLAGS, "grain" },
    { "gsize",  "The size of grain to be added",  OFFSET(grainSize),  AV_OPT_TYPE_DOUBLE, {.dbl=0}, 0.0, 5.0, FLAGS, "gsize" },
    { "kcolor",  "Run extra color correction if required by model",  OFFSET(canKeepColor),  AV_OPT_TYPE_INT, {.i64=1}, 0, 1, FLAGS, "kcolor" },
    { "blend",  "The amount of input to be blended with output",  OFFSET(blend),  AV_OPT_TYPE_DOUBLE, {.dbl=0}, 0.0, 1.0, FLAGS, "blend" },
    { "parameters", TVAI_UPSCALE_PARAMETER_MESSAGE, OFFSET(parameters), AV_OPT_TYPE_DICT, {.str=""}, .flags = FLAGS, "parameters" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(tvai_up);

static av_cold int init(AVFilterContext *ctx) {
  TVAIUpContext *tvai = ctx->priv;
  av_log(ctx, AV_LOG_VERBOSE, "Here init with params: %s %d %d %lf %lf %lf %lf %lf %lf\n", tvai->basicInfo.modelName, tvai->basicInfo.scale, tvai->basicInfo.device.index,
        tvai->preBlur, tvai->noise, tvai->details, tvai->halo, tvai->blur, tvai->compression);
  tvai->previousFrame = NULL;
  tvai->count = 0;
  return 0;
}

static int config_props(AVFilterLink *outlink) {
    AVFilterContext *ctx = outlink->src;
    TVAIUpContext *tvai = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    av_dict_set_float(&tvai->parameters, "preBlur", tvai->preBlur, 0);
    av_dict_set_float(&tvai->parameters, "noise", tvai->noise, 0);
    av_dict_set_float(&tvai->parameters, "details", tvai->details, 0);
    av_dict_set_float(&tvai->parameters, "halo", tvai->halo, 0);
    av_dict_set_float(&tvai->parameters, "blur", tvai->blur, 0);
    av_dict_set_float(&tvai->parameters, "compression", tvai->compression, 0);
    av_dict_set_float(&tvai->parameters, "prenoise", tvai->prenoise, 0);
    av_dict_set_float(&tvai->parameters, "grain", tvai->grain, 0);
    av_dict_set_float(&tvai->parameters, "grainSize", tvai->grainSize, 0);
    av_dict_set_float(&tvai->parameters, "blend", tvai->blend, 0);
    
    av_dict_set_int(&tvai->parameters, "canKeepColor", tvai->canKeepColor, 0);
    tvai->modelParameters = ff_tvai_alloc_copy_entries(tvai->parameters, &(tvai->modelParameterCount));
    VideoProcessorInfo info;
    double sar = av_q2d(inlink->sample_aspect_ratio) > 0 ? av_q2d(inlink->sample_aspect_ratio) : 1;
    if(tvai->basicInfo.scale == 0) {
      float x = tvai->w/inlink->w, y = tvai->h*1.0f/inlink->h;
      float v = x > y ? x : y;
      tvai->basicInfo.scale = (v > 2.4) ? 4 : (v > 1.2 ? 2 : 1);
      av_log(ctx, AV_LOG_VERBOSE, "SAR: %lf scale: %d x: %f y: %f v: %f\n", sar, tvai->basicInfo.scale, x, y, v);
    }
    info.frameCount = tvai->estimateFrameCount;
    av_log(ctx, AV_LOG_VERBOSE, "Here init with perf options: model: %s scale: %d device: %d vram: %lf threads: %d downloads: %d\n", info.basic.modelName, info.basic.scale, 
            info.basic.device.index, info.basic.device.maxMemory, info.basic.device.extraThreadCount, info.basic.canDownloadModel);
    ff_av_dict_log(ctx, "Parameters", tvai->parameters);
    if(ff_tvai_prepareProcessorInfo(tvai->deviceString, &info, ModelTypeUpscaling, outlink, &(tvai->basicInfo), tvai->estimateFrameCount > 0, tvai->modelParameters, tvai->modelParameterCount)) {
        return AVERROR(EINVAL);
      return AVERROR(EINVAL);  
    }
    tvai->pFrameProcessor = tvai_create(&info);
    tvai->previousFrame = NULL;
    return tvai->pFrameProcessor == NULL ? AVERROR(EINVAL) : 0;
}

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_RGB48,
    AV_PIX_FMT_NONE
};

static int filter_frame(AVFilterLink *inlink, AVFrame *in) {
    AVFilterContext *ctx = inlink->dst;
    TVAIUpContext *tvai = ctx->priv;
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
    TVAIUpContext *tvai = ctx->priv;
    int ret = ff_request_frame(ctx->inputs[0]);
    if (ret == AVERROR_EOF) {
        int r = ff_tvai_postflight(outlink, tvai->pFrameProcessor, tvai->previousFrame);
        if(r)
            return r;
    }
    return ret;
}

static av_cold void uninit(AVFilterContext *ctx) {
    TVAIUpContext *tvai = ctx->priv;
    av_log(ctx, AV_LOG_DEBUG, "Uninit called for %s %d\n", tvai->basicInfo.modelName, tvai->pFrameProcessor == NULL);
    if(tvai->pFrameProcessor)
        tvai_destroy(tvai->pFrameProcessor);
}

static const AVFilterPad tvai_up_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
};

static const AVFilterPad tvai_up_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = config_props,
        .request_frame = request_frame,
    },
};

const FFFilter ff_vf_tvai_up = {
    .p.name          = "tvai_up",
    .p.description   = NULL_IF_CONFIG_SMALL("Apply Topaz Video AI upscale models, parameters will only be applied to appropriate models"),
    .priv_size     = sizeof(TVAIUpContext),
    .init          = init,
    .uninit        = uninit,
    FILTER_INPUTS(tvai_up_inputs),
    FILTER_OUTPUTS(tvai_up_outputs),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .p.priv_class    = &tvai_up_class,
    .p.flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
