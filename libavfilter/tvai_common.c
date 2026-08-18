#include "tvai_common.h"
#include <libavutil/mem.h>
#include <stdlib.h>

#if CONFIG_FFNVCODEC
#include "libavutil/cuda_check.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_cuda_internal.h"
#include "libavutil/pixdesc.h"

// What a CUDA frame may hold for a tvai filter, and what the library calls it. The library converts
// each of these to and from its own float frames on the device, so a frame in any of them never
// comes across the bus; anything else would have to, which is what handing frames over on the device
// is meant to avoid. NV12 and P010 are what nvdec hands out and nvenc takes, so a decode to encode
// run needs no conversion filter at all.
static const struct {
    enum AVPixelFormat format;
    TVAIPixelFormat pixelFormat;
} tvai_device_formats[] = {
    {AV_PIX_FMT_NV12, TVAIPixelFormatNV12},
    {AV_PIX_FMT_P010, TVAIPixelFormatP010},
    {AV_PIX_FMT_RGB48, TVAIPixelFormatRGB16},
    {AV_PIX_FMT_RGB24, TVAIPixelFormatRGB8},
};

// What the graph says about a frame's colour, in the library's terms. Unspecified is passed on as
// such rather than guessed at here, so that the guess a YUV frame needs lives in one place, beside
// the conversion that depends on it.
static TVAIColorMatrix tvai_color_matrix(enum AVColorSpace space) {
    switch (space) {
        case AVCOL_SPC_BT470BG:
        case AVCOL_SPC_SMPTE170M:
        case AVCOL_SPC_SMPTE240M:
            return TVAIColorMatrixBT601;
        case AVCOL_SPC_BT709:
            return TVAIColorMatrixBT709;
        case AVCOL_SPC_BT2020_NCL:
        case AVCOL_SPC_BT2020_CL:
            return TVAIColorMatrixBT2020;
        default:
            return TVAIColorMatrixUnspecified;
    }
}

static TVAIColorRange tvai_color_range(enum AVColorRange range) {
    switch (range) {
        case AVCOL_RANGE_MPEG:
            return TVAIColorRangeLimited;
        case AVCOL_RANGE_JPEG:
            return TVAIColorRangeFull;
        default:
            return TVAIColorRangeUnspecified;
    }
}

// Describes one of the graph's frames to the library: where each of its planes is on the device, when
// it belongs in the stream, and how to read its samples as colour.
static void tvai_describe_device_frame(TVAIDeviceBuffer *pBuffer, const AVFrame *frame, enum AVColorSpace space,
        enum AVColorRange range, long long pts, long long duration) {
    int i;
    memset(pBuffer, 0, sizeof(*pBuffer));
    for (i = 0; i < TVAI_MAX_PLANES && i < AV_NUM_DATA_POINTERS; i++) {
        pBuffer->pPlanes[i] = frame->data[i];
        pBuffer->lineSizes[i] = (size_t)frame->linesize[i];
    }
    pBuffer->pts = pts;
    pBuffer->duration = duration;
    pBuffer->deviceIndex = -1;
    pBuffer->colorMatrix = tvai_color_matrix(space);
    pBuffer->colorRange = tvai_color_range(range);
}

// The GPU a frame is on, by the index the library knows it by. Its context is not the one the
// library works in, so this asks the driver rather than assuming they agree.
static int tvai_device_index(void *pLogCtx, const AVFrame *frame, int *pIndex) {
    AVHWFramesContext *pFrames = (AVHWFramesContext*)frame->hw_frames_ctx->data;
    AVCUDADeviceContext *pDevice = pFrames->device_ctx->hwctx;
    CudaFunctions *cu = pDevice->internal->cuda_dl;
    CUcontext dummy;
    CUdevice device = 0;
    int ret = FF_CUDA_CHECK_DL(pLogCtx, cu, cu->cuCtxPushCurrent(pDevice->cuda_ctx));
    if (ret < 0)
        return ret;
    ret = FF_CUDA_CHECK_DL(pLogCtx, cu, cu->cuCtxGetDevice(&device));
    FF_CUDA_CHECK_DL(pLogCtx, cu, cu->cuCtxPopCurrent(&dummy));
    if (ret < 0)
        return ret;
    *pIndex = (int)device;
    return 0;
}

// The library copies an incoming frame on a stream of its own, which is not ordered against the one
// filters upstream leave their work on, so whatever is still writing the frame has to finish first.
static int tvai_await_device_frame(void *pLogCtx, const AVFrame *frame) {
    AVHWFramesContext *pFrames = (AVHWFramesContext*)frame->hw_frames_ctx->data;
    AVCUDADeviceContext *pDevice = pFrames->device_ctx->hwctx;
    CudaFunctions *cu = pDevice->internal->cuda_dl;
    CUcontext dummy;
    int ret = FF_CUDA_CHECK_DL(pLogCtx, cu, cu->cuCtxPushCurrent(pDevice->cuda_ctx));
    if (ret < 0)
        return ret;
    ret = FF_CUDA_CHECK_DL(pLogCtx, cu, cu->cuStreamSynchronize(pDevice->stream));
    FF_CUDA_CHECK_DL(pLogCtx, cu, cu->cuCtxPopCurrent(&dummy));
    return ret;
}

// Points the output link at frames of its own to be filled in, and tells the library what the ones
// coming in hold. The output frames match the input's format on the same device, at whatever size
// the filter has settled on for its output.
static int tvai_prepare_device_frames(AVFilterLink *pOutlink, VideoProcessorInfo *pInfo) {
    AVFilterContext *pCtx = pOutlink->src;
    FilterLink *fInlink = ff_filter_link(pCtx->inputs[0]);
    FilterLink *fOutlink = ff_filter_link(pOutlink);
    AVHWFramesContext *pInFrames, *pOutFrames;
    size_t i;
    int ret;

    if (fInlink->hw_frames_ctx == NULL) {
        av_log(pCtx, AV_LOG_ERROR, "No hardware frame context on the input\n");
        return AVERROR(EINVAL);
    }
    pInFrames = (AVHWFramesContext*)fInlink->hw_frames_ctx->data;
    for (i = 0; i < FF_ARRAY_ELEMS(tvai_device_formats); i++)
        if (tvai_device_formats[i].format == pInFrames->sw_format)
            break;
    if (i == FF_ARRAY_ELEMS(tvai_device_formats)) {
        av_log(pCtx, AV_LOG_ERROR, "Cannot take frames holding %s on a device, convert them to %s first\n",
               av_get_pix_fmt_name(pInFrames->sw_format), av_get_pix_fmt_name(tvai_device_formats[0].format));
        return AVERROR(ENOSYS);
    }
    pInfo->basic.pixelFormat = tvai_device_formats[i].pixelFormat;

    fOutlink->hw_frames_ctx = av_hwframe_ctx_alloc(pInFrames->device_ref);
    if (fOutlink->hw_frames_ctx == NULL)
        return AVERROR(ENOMEM);
    pOutFrames = (AVHWFramesContext*)fOutlink->hw_frames_ctx->data;
    pOutFrames->format = AV_PIX_FMT_CUDA;
    pOutFrames->sw_format = pInFrames->sw_format;
    pOutFrames->width = pOutlink->w;
    pOutFrames->height = pOutlink->h;
    // Interpolation answers one frame with several, and each is held until the graph has taken it,
    // so the pool has to be deeper than the one frame at a time an upscale needs.
    pOutFrames->initial_pool_size = 8;
    ret = ff_filter_init_hw_frames(pCtx, pOutlink, 10);
    if (ret < 0)
        return ret;
    return av_hwframe_ctx_init(fOutlink->hw_frames_ctx);
}
#endif

int ff_tvai_device_frames(void) {
#if CONFIG_FFNVCODEC
    const char *asked = getenv("TVAI_USE_GPU");
    // A frame that is already on a device stays there, which is what a decoder and encoder on the
    // same GPU want. TVAI_USE_GPU=0 is the way back to host frames.
    return asked == NULL || strcmp(asked, "0") != 0;
#else
    return 0;
#endif
}

int ff_tvai_query_formats(const AVFilterContext *ctx, AVFilterFormatsConfig **cfg_in,
        AVFilterFormatsConfig **cfg_out, enum AVPixelFormat format) {
    // CUDA first, so a graph whose frames are on a device keeps them there rather than settling on
    // the host format both ends can also reach. The host format stays in the list, which is what a
    // graph with no device in it negotiates, there being no way to reach CUDA from one.
    int formats[3] = {AV_PIX_FMT_NONE, AV_PIX_FMT_NONE, AV_PIX_FMT_NONE};
    int count = 0;
    if (ff_tvai_device_frames())
        formats[count++] = AV_PIX_FMT_CUDA;
    formats[count] = format;
    return ff_set_common_formats_from_list2(ctx, cfg_in, cfg_out, formats);
}

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
#if CONFIG_FFNVCODEC
  if(pInlink->format == AV_PIX_FMT_CUDA && tvai_prepare_device_frames(pOutlink, pProcessorInfo) < 0)
    return 1;
#endif
  return 0;
}

int ff_tvai_process(void *pFrameProcessor, AVFrame* frame) {
    return ff_tvai_process_timed(pFrameProcessor, frame, frame->pts, frame->duration);
}

int ff_tvai_process_timed(void *pFrameProcessor, AVFrame* frame, long long pts, long long duration) {
    TVAIBuffer iBuffer;
    if(pFrameProcessor == NULL)
        return 1;
#if CONFIG_FFNVCODEC
    if(frame->format == AV_PIX_FMT_CUDA) {
        TVAIDeviceBuffer dBuffer;
        tvai_describe_device_frame(&dBuffer, frame, frame->colorspace, frame->color_range, pts, duration);
        if(tvai_device_index(NULL, frame, &dBuffer.deviceIndex) < 0 || tvai_await_device_frame(NULL, frame) < 0)
            return 1;
        return tvai_process_device_frame(pFrameProcessor, &dBuffer) != 0;
    }
#endif
    ff_tvai_prepareBufferInput(&iBuffer, frame);
    iBuffer.pts = pts;
    iBuffer.duration = duration;
    return tvai_process(pFrameProcessor, &iBuffer) != 0;
}

// Fills a frame the graph has handed out, from the device when that is where it lives. Nothing is
// queued against a frame straight out of the pool, so there is nothing to wait for on the way out;
// the library leaves the pixels in place before it returns.
static int ff_tvai_output_into(void *pProcessor, AVFilterLink *outlink, AVFrame *out, TVAIBuffer *pBuffer) {
#if CONFIG_FFNVCODEC
    if(out->format == AV_PIX_FMT_CUDA) {
        TVAIDeviceBuffer dBuffer;
        // A frame straight out of the pool carries no properties yet, av_frame_copy_props running
        // once it is filled, so the colour it should be written in comes from the link instead.
        tvai_describe_device_frame(&dBuffer, out, outlink->colorspace, outlink->color_range, 0, 0);
        if(tvai_device_index(outlink->src, out, &dBuffer.deviceIndex) < 0 ||
                tvai_output_device_frame(pProcessor, &dBuffer))
            return 1;
        pBuffer->pts = dBuffer.pts;
        pBuffer->duration = dBuffer.duration;
        pBuffer->frameNo = dBuffer.frameNo;
        return 0;
    }
#endif
    return tvai_output_frame(pProcessor, pBuffer) != 0;
}

int ff_tvai_add_output(void *pProcessor, AVFilterLink *outlink, AVFrame* frame) {
    int n = tvai_output_count(pProcessor), i;
    for(i=0;i<n;i++) {
        TVAIBuffer oBuffer;
        AVFrame *out = ff_tvai_prepareBufferOutput(outlink, &oBuffer);
        if(out != NULL && ff_tvai_output_into(pProcessor, outlink, out, &oBuffer) == 0) {
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


