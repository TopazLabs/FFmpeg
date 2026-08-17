#include "tvai_common.h"
#include <libavutil/mem.h>

int ff_tvai_checkDevice(char* deviceString, DeviceSetting* pDevice, AVFilterContext* ctx) {
  if(tvai_set_device_settings(deviceString, pDevice)) {
      char devices[1024];
      int device_count = tvai_device_list(devices, 1024);
      av_log(ctx, AV_LOG_ERROR, "Invalid value %s for device, device should be in the following list:\n-2 : AUTO \n-1 : CPU\n%s\n%d : ALL GPUs\n", deviceString, devices, device_count);
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
  tvai_set_logging(logLevel > AV_LOG_INFO);
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

void ff_tvai_prepareBufferInput(TVAIBuffer* ioBuffer, AVFrame *in) {
  ioBuffer->pBuffer = in->data[0];
  ioBuffer->lineSize = in->linesize[0];
  ioBuffer->pts = in->pts;
  ioBuffer->duration = in->duration;
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

int ff_tvai_prepareProcessorInfo(char *deviceString, VideoProcessorInfo* pProcessorInfo, ModelType modelType, AVFilterLink *pOutlink, BasicProcessorInfo* pBasic, int procIndex, DictionaryItem *pParameters, int parameterCount) {
  ff_tvai_handleLogging();
  AVFilterContext *pCtx = pOutlink->src;
  AVFilterLink *pInlink = pCtx->inputs[0];
  FilterLink *fInlink = ff_filter_link(pInlink);
  FilterLink *fOutlink = ff_filter_link(pOutlink);
  pProcessorInfo->basic = *pBasic;
  if(ff_tvai_checkModel(pProcessorInfo->basic.modelName, modelType, pCtx) || ff_tvai_checkDevice(deviceString, &(pProcessorInfo->basic.device), pCtx) || ff_tvai_checkScale(pProcessorInfo->basic.modelName, pProcessorInfo->basic.scale, pCtx)) {
    return 1;
  }
  tvai_vp_name(pProcessorInfo->basic.modelName, procIndex, (char*)pProcessorInfo->basic.processorName);
  pProcessorInfo->basic.preflight = 0;
  pProcessorInfo->basic.pixelFormat = TVAIPixelFormatRGB16;
  pProcessorInfo->basic.inputWidth = pInlink->w;
  pProcessorInfo->basic.inputHeight = pInlink->h;
  pProcessorInfo->basic.timebase = av_q2d(pInlink->time_base);
  pProcessorInfo->basic.framerate = av_q2d(fInlink->frame_rate);
  pProcessorInfo->outputWidth = pOutlink->w = pInlink->w*pProcessorInfo->basic.scale;
  pProcessorInfo->outputHeight = pOutlink->h = pInlink->h*pProcessorInfo->basic.scale;
  pProcessorInfo->basic.pParameters = pParameters;
  pProcessorInfo->basic.parameterCount = parameterCount;
  pOutlink->time_base = pInlink->time_base;
  fOutlink->frame_rate = fInlink->frame_rate;
  pOutlink->sample_aspect_ratio = pInlink->sample_aspect_ratio;
  return 0;
}

int ff_tvai_process(void *pFrameProcessor, AVFrame* frame) {
    TVAIBuffer iBuffer;
    ff_tvai_prepareBufferInput(&iBuffer, frame);
    if(pFrameProcessor == NULL || tvai_process(pFrameProcessor, &iBuffer)) 
        return 1;
    return 0;
}

int ff_tvai_add_output(void *pProcessor, AVFilterLink *outlink, AVFrame* frame) {
    int n = tvai_output_count(pProcessor), i;
    for(i=0;i<n;i++) {
        TVAIBuffer oBuffer;
        AVFrame *out = ff_tvai_prepareBufferOutput(outlink, &oBuffer);
        if(out != NULL && tvai_output_frame(pProcessor, &oBuffer) == 0) {
            av_frame_copy_props(out, frame);
            out->duration = oBuffer.duration;
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
        tvai_output_frame(pProcessor, &oBuffer);
        av_log(NULL, AV_LOG_DEBUG, "Ignoring output frame %d %d\n", i, n);
    }
}

int ff_tvai_copy_entries(AVDictionary* dict, DictionaryItem* pDictInfo) {
    AVDictionaryEntry *entry = NULL;
    int i=0;
    while ((entry = av_dict_get(dict, "", entry, AV_DICT_IGNORE_SUFFIX))) {
        pDictInfo[i].pKey = entry->key;
        pDictInfo[i++].pValue = entry->value;
        av_log(NULL, AV_LOG_DEBUG, "COPYING %d %s: %s\n", i, entry->key, entry->value);
    }  
    return i;
}

void ff_av_dict_log(AVFilterContext *ctx, const char* msg, const AVDictionary *dict) {
    AVDictionaryEntry *entry = NULL;
    while ((entry = av_dict_get(dict, "", entry, AV_DICT_IGNORE_SUFFIX))) {
        av_log(ctx, AV_LOG_DEBUG, "%s %s: %s\n", msg, entry->key, entry->value);
    }
}

void av_dict_set_float(AVDictionary **dict, const char *key, float value, int flag) {
    char valueStr[32];
    snprintf(valueStr, sizeof(valueStr), "%f", value);
    av_dict_set(dict, key, valueStr, flag);
}

DictionaryItem* ff_tvai_alloc_copy_entries(AVDictionary* dict, int *pCount) {
    int count = av_dict_count(dict);
    DictionaryItem *pDictInfo = (DictionaryItem*)av_malloc(sizeof(DictionaryItem) + sizeof(DictionaryItem)*count);
    *pCount = ff_tvai_copy_entries(dict, pDictInfo);
    return pDictInfo;
}

int ff_tvai_postflight(AVFilterLink *outlink, void* pFrameProcessor, AVFrame* previousFrame) {
    tvai_end_stream(pFrameProcessor);
    int i = 0, remaining = tvai_remaining_frames(pFrameProcessor), pr = 0;
    unsigned int timeout_count = tvai_timeout_count(pFrameProcessor, remaining);
    while(remaining > 0 && i < timeout_count) {
        int ret = ff_tvai_add_output(pFrameProcessor, outlink, previousFrame);
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


