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

static void tvai_frame_release_cb(void *opaque, uint8_t *data) {
    tvai_release_frame(opaque);
}

/* Zero-copy output: wrap an SDK-owned frame (tvai_output_frame_ref) into an
 * AVFrame whose buffer returns to the SDK's frame pool on last unref. Only
 * possible for rgbf32, where the SDK's internal FP32 frame matches the link
 * pixel format bit-for-bit. Returns NULL if the SDK has no frame to hand out
 * (caller falls back to the copying path). */
static AVFrame* tvai_wrap_output_frame(void *pProcessor, AVFilterLink *outlink, TVAIBuffer* oBuffer) {
    void *handle = NULL;
    AVFrame *out;
    if (tvai_output_frame_ref(pProcessor, oBuffer, &handle))
        return NULL;
    out = av_frame_alloc();
    if (!out) {
        tvai_release_frame(handle);
        return NULL;
    }
    out->format = outlink->format;
    out->width  = outlink->w;
    out->height = outlink->h;
    /* NOTE: the SDK pool memory is currently not CUDA-pinned. If the SDK ever
     * pins its OUT_FRAME pool, hwupload's host->device copy becomes truly
     * asynchronous and freeing this buffer right after av_hwframe_transfer_data
     * would race the DMA; the transfer would then need a sync or a held ref. */
    out->buf[0] = av_buffer_create(oBuffer->pBuffer, oBuffer->lineSize * outlink->h,
                                   tvai_frame_release_cb, handle, 0);
    if (!out->buf[0]) {
        av_frame_free(&out);
        tvai_release_frame(handle);
        return NULL;
    }
    out->data[0]     = oBuffer->pBuffer;
    out->linesize[0] = oBuffer->lineSize;
    out->sample_aspect_ratio = outlink->sample_aspect_ratio;
    out->colorspace  = outlink->colorspace;
    out->color_range = outlink->color_range;
    return out;
}

#define TVAI_POOL_ALIGN 64

static void tvai_pool_buffer_free(void *opaque, uint8_t *data) {
  tvai_free_buffer(data);
}

static AVBufferRef *tvai_pool_alloc_cb(size_t size) {
  void *p = tvai_alloc_buffer(size);
  AVBufferRef *ref;
  if (!p)
      return NULL;
  ref = av_buffer_create(p, size, tvai_pool_buffer_free, NULL, 0);
  if (!ref)
      tvai_free_buffer(p);
  return ref;
}

AVFrame* ff_tvai_pool_frame(AVBufferPool **pPool, AVFilterLink *link, int format, int w, int h) {
  AVFrame *frame;
  if (!*pPool) {
      int size = av_image_get_buffer_size(format, w, h, TVAI_POOL_ALIGN);
      if (size < 0)
          return NULL;
      *pPool = av_buffer_pool_init(size, tvai_pool_alloc_cb);
      if (!*pPool)
          return NULL;
  }
  frame = av_frame_alloc();
  if (!frame)
      return NULL;
  frame->buf[0] = av_buffer_pool_get(*pPool);
  if (!frame->buf[0])
      goto fail;
  if (av_image_fill_arrays(frame->data, frame->linesize, frame->buf[0]->data,
                           format, w, h, TVAI_POOL_ALIGN) < 0)
      goto fail;
  frame->format = format;
  frame->width  = w;
  frame->height = h;
  frame->sample_aspect_ratio = link->sample_aspect_ratio;
  frame->colorspace  = link->colorspace;
  frame->color_range = link->color_range;
  frame->alpha_mode  = link->alpha_mode;
  return frame;
fail:
  av_frame_free(&frame);
  return NULL;
}

AVFrame *ff_tvai_get_in_buffer(TVAIInPool *p, AVFilterLink *inlink, int w, int h) {
  AVFrame *frame;
  if (p->pool && (w != p->w || h != p->h || inlink->format != p->fmt))
      av_buffer_pool_uninit(&p->pool);
  p->w   = w;
  p->h   = h;
  p->fmt = inlink->format;
  frame = ff_tvai_pool_frame(&p->pool, inlink, inlink->format, w, h);
  if (!frame)
      frame = ff_default_get_video_buffer(inlink, w, h);
  return frame;
}

void ff_tvai_in_pool_uninit(TVAIInPool *p) {
  av_buffer_pool_uninit(&p->pool);
}

AVFrame* ff_tvai_prepareBufferOutput(AVFilterLink *outlink, TVAIBuffer* oBuffer, AVBufferPool **pPool) {
  AVFrame* out = NULL;
  if (pPool)
      out = ff_tvai_pool_frame(pPool, outlink, outlink->format, outlink->w, outlink->h);
  if (!out)
      out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
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
  switch (pInlink->format) {
  case AV_PIX_FMT_RGBF32:
      pProcessorInfo->basic.pixelFormat = TVAIPixelFormatRGB32F;
      break;
  case AV_PIX_FMT_RGBAF32:
      pProcessorInfo->basic.pixelFormat = TVAIPixelFormatRGBA32F;
      break;
  default:
      pProcessorInfo->basic.pixelFormat = TVAIPixelFormatRGB16;
      break;
  }
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

int ff_tvai_add_output(void *pProcessor, AVFilterLink *outlink, AVFrame* frame, AVBufferPool **pPool) {
    int n = tvai_output_count(pProcessor), i;
    for(i=0;i<n;i++) {
        TVAIBuffer oBuffer = {0};
        AVFrame *out = NULL;
        if (outlink->format == AV_PIX_FMT_RGBF32)
            out = tvai_wrap_output_frame(pProcessor, outlink, &oBuffer);
        if (out == NULL) {
            out = ff_tvai_prepareBufferOutput(outlink, &oBuffer, pPool);
            if (out != NULL && tvai_output_frame(pProcessor, &oBuffer) != 0)
                av_frame_free(&out);
        }
        if(out != NULL) {
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

int ff_tvai_postflight(AVFilterLink *outlink, void* pFrameProcessor, AVFrame* previousFrame, AVBufferPool **pPool) {
    tvai_end_stream(pFrameProcessor);
    int i = 0, remaining = tvai_remaining_frames(pFrameProcessor), pr = 0;
    unsigned int timeout_count = tvai_timeout_count(pFrameProcessor, remaining);
    while(remaining > 0 && i < timeout_count) {
        int ret = ff_tvai_add_output(pFrameProcessor, outlink, previousFrame, pPool);
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


