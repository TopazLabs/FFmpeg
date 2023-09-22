#include "tvai_common.h"

int ff_tvai_checkDevice(int deviceIndex, AVFilterContext* ctx) {
  char devices[1024];
  int device_count = tvai_device_list(devices, 1024);
  if(deviceIndex < -2 || deviceIndex > device_count ) {
      av_log(ctx, AV_LOG_ERROR, "Invalid value %d for device, device should be in the following list:\n-2 : AUTO \n-1 : CPU\n%s\n%d : ALL GPUs\n", deviceIndex, devices, device_count);
      return AVERROR(EINVAL);
  }
  return 0;
}

int ff_tvai_checkScale(char* modelName, int scale, AVFilterContext* ctx) {
  char scaleString[1024];
  int retVal = tvai_scale_list(modelName, scale, scaleString, 1024);
  if(retVal > 0) {
      av_log(ctx, AV_LOG_ERROR, "Invalid scale %d for model %s, allowed scales are: %s\n", scale, modelName, scaleString);
      return AVERROR(EINVAL);
  } else if(retVal < 0) {
    av_log(ctx, AV_LOG_ERROR, "Model not found: %s\n", modelName);
    return AVERROR(EINVAL);
  }
  return 0;
}

void ff_tvai_handleLogging() {
  int logLevel = av_log_get_level();
  tvai_set_logging(logLevel == AV_LOG_DEBUG || logLevel == AV_LOG_VERBOSE);
}

int ff_tvai_checkModel(char* modelName, ModelType modelType, AVFilterContext* ctx) {
  char modelString[10024];
  int modelStringSize = tvai_model_list(modelName, modelType, modelString, 10024);
  if(modelStringSize > 0) {
      av_log(ctx, AV_LOG_ERROR, "Invalid value %s for model, model should be in the following list:\n%s\n", modelName, modelString);
      return AVERROR(EINVAL);
  } else if(modelStringSize < 0) {
    av_log(ctx, AV_LOG_ERROR, "Some other error:%s\n", modelString);
    return AVERROR(EINVAL);
  }
  return 0;
}

int ff_tvai_verifyAndSetInfo(VideoProcessorInfo* info, AVFilterLink *inlink, AVFilterLink *outlink, unsigned int procIndex, char* modelName, ModelType modelType,
                            int deviceIndex, int extraThreads, float vram, int scale, int canDownloadModels, float *pParameters, int parameterCount, AVFilterContext* ctx) {
  ff_tvai_handleLogging();
  if(ff_tvai_checkModel(modelName, modelType, ctx) || ff_tvai_checkDevice(deviceIndex, ctx) || ff_tvai_checkScale(modelName, scale, ctx)) {
    return 1;
  }
  tvai_vp_name(modelName, procIndex, (char*)info->basic.processorName);
  info->basic.modelName = modelName;
  info->basic.scale = scale;
  info->basic.device.index = deviceIndex;
  info->basic.device.extraThreadCount = extraThreads;
  info->basic.device.maxMemory = vram;
  info->basic.canDownloadModel = canDownloadModels;
  info->basic.inputWidth = inlink->w;
  info->basic.inputHeight = inlink->h;
  info->basic.timebase = av_q2d(inlink->time_base);
  info->basic.framerate = av_q2d(inlink->frame_rate);
  if(pParameters != NULL && parameterCount > 0) {
    memcpy(info->modelParameters, pParameters, sizeof(float)*parameterCount);
  }
  outlink->w = inlink->w*scale;
  outlink->h = inlink->h*scale;
  outlink->time_base = inlink->time_base;
  outlink->frame_rate = inlink->frame_rate;
  outlink->sample_aspect_ratio = inlink->sample_aspect_ratio;
  av_log(ctx, AV_LOG_DEBUG, "Output size set to: %d %d\n", outlink->w, outlink->h);
  av_log(ctx, AV_LOG_DEBUG, "Here Config props model with params: %s %s %d %d %d %d %d %d %lf %lf\n", info->basic.processorName, info->basic.modelName, info->basic.scale, info->basic.device.index,
          info->basic.device.extraThreadCount, info->basic.canDownloadModel, info->basic.inputWidth, info->basic.inputHeight, info->basic.timebase, info->basic.framerate);
  return 0;
}

void* ff_tvai_verifyAndCreate(AVFilterLink *inlink, AVFilterLink *outlink, char *processorName, char* modelName, ModelType modelType,
                            int deviceIndex, int extraThreads, float vram, int scale, int canDownloadModels, float *pParameters, int parameterCount, AVFilterContext* ctx) {
  VideoProcessorInfo info;
  if(ff_tvai_verifyAndSetInfo(&info, inlink, outlink, processorName, modelName, modelType, deviceIndex, extraThreads, vram, scale, canDownloadModels, pParameters, parameterCount, ctx))
    return NULL;
  return tvai_create(&info);
}

void ff_tvai_prepareBufferInput(TVAIBuffer* ioBuffer, AVFrame *in) {
  ioBuffer->pBuffer = in->data[0];
  ioBuffer->lineSize = in->linesize[0];
  ioBuffer->pts = in->pts;
}

AVFrame* ff_tvai_prepareBufferOutput(AVFilterLink *outlink, TVAIBuffer* oBuffer) {
  AVFrame* out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
  if (!out) {
      av_log(NULL, AV_LOG_ERROR, "The processing has failed, unable to create output buffer of size:%dx%d\n", outlink->w, outlink->h);
      return NULL;
  }
  oBuffer->pBuffer = out->data[0];
  oBuffer->lineSize = out->linesize[0];
  return out;
}

int ff_tvai_prepareProcessorInfo(VideoProcessorInfo* pProcessorInfo, ModelType modelType, AVFilterLink *pOutlink, BasicProcessorInfo* pBasic, int procIndex, float *pParameters, int parameterCount) {
  ff_tvai_handleLogging();
  AVFilterContext *pCtx = pOutlink->src;
  AVFilterLink *pInlink = pCtx->inputs[0];
  pProcessorInfo->basic = *pBasic;
  if(ff_tvai_checkModel(pProcessorInfo->basic.modelName, modelType, pCtx) || ff_tvai_checkDevice(pProcessorInfo->basic.device.index, pCtx) || ff_tvai_checkScale(pProcessorInfo->basic.modelName, pProcessorInfo->basic.scale, pCtx)) {
    return 1;
  }
  tvai_vp_name(pProcessorInfo->basic.modelName, procIndex, (char*)pProcessorInfo->basic.processorName);
  pProcessorInfo->basic.inputWidth = pInlink->w;
  pProcessorInfo->basic.inputHeight = pInlink->h;
  pProcessorInfo->basic.timebase = av_q2d(pInlink->time_base);
  pProcessorInfo->basic.framerate = av_q2d(pInlink->frame_rate);
  if(pParameters != NULL && parameterCount > 0) {
    memcpy(pProcessorInfo->modelParameters, pParameters, sizeof(float)*parameterCount);
  }
  pOutlink->w = pInlink->w*pProcessorInfo->basic.scale;
  pOutlink->h = pInlink->h*pProcessorInfo->basic.scale;
  pOutlink->time_base = pInlink->time_base;
  pOutlink->frame_rate = pInlink->frame_rate;
  pOutlink->sample_aspect_ratio = pInlink->sample_aspect_ratio;
  return 0;
}

int ff_tvai_process(void *pFrameProcessor, AVFrame* frame, int copy) {
    TVAIBuffer iBuffer;
    ff_tvai_prepareBufferInput(&iBuffer, frame);
    if(pFrameProcessor == NULL || tvai_process(pFrameProcessor, &iBuffer, copy)) 
        return 1;
    return 0;
}

int ff_tvai_add_output(void *pProcessor, AVFilterLink *outlink, AVFrame* frame, int copy) {
    int n = tvai_output_count(pProcessor), i;
    for(i=0;i<n;i++) {
        TVAIBuffer oBuffer;
        AVFrame *out = ff_tvai_prepareBufferOutput(outlink, &oBuffer);
        if(out != NULL && tvai_output_frame(pProcessor, &oBuffer, copy) == 0) {
            av_frame_copy_props(out, frame);
            out->pts = oBuffer.pts;
            int ret = 0;
            if(oBuffer.pts >= 0)
                ret = ff_filter_frame(outlink, out);
            if(oBuffer.pts < 0 || ret) {
                av_frame_free(&out);
                av_log(NULL, AV_LOG_ERROR, "Ignoring frame %ld %ld %lf\n", oBuffer.pts, frame->pts, TS2T(oBuffer.pts, outlink->time_base));
                return ret;
            }
            av_log(NULL, AV_LOG_DEBUG, "Finished processing frame %ld %ld %lf\n", oBuffer.pts, frame->pts, TS2T(oBuffer.pts, outlink->time_base));
        } else {
            av_log(NULL, AV_LOG_ERROR, "Error processing frame %ld %ld %lf\n", oBuffer.pts, frame->pts, TS2T(oBuffer.pts, outlink->time_base));
            return AVERROR(ENOSYS);
        }
    }
    return 0;
}

void ff_tvai_ignore_output(void *pProcessor) {
    int n = tvai_output_count(pProcessor), i;
    for(i=0;i<n;i++) {
        TVAIBuffer oBuffer;
        tvai_output_frame(pProcessor, &oBuffer, 1);
        av_log(NULL, AV_LOG_DEBUG, "Ignoring output frame %d %d\n", i, n);
    }
}

int ff_tvai_postflight(AVFilterLink *outlink, void* pFrameProcessor, AVFrame* previousFrame) {
    tvai_end_stream(pFrameProcessor);
    int i = 0, remaining = tvai_remaining_frames(pFrameProcessor), pr = 0;
    while(remaining > 0 && i < 50) {
        int ret = ff_tvai_add_output(pFrameProcessor, outlink, previousFrame, 0);
        if(ret)
            return ret;
        tvai_wait(500);
        pr = remaining;
        remaining = tvai_remaining_frames(pFrameProcessor);
        if(pr == remaining)
            i++;
        else
            i = 0;
    }
    if(remaining > 0) {
        av_log(NULL, AV_LOG_WARNING, "Waited too long for processing, ending file %d\n", remaining);    
    }
    return 0;
}


