#ifndef TVAI_COMMON_H
#define TVAI_COMMON_H

#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/avutil.h"
#include "avfilter.h"
#include "formats.h"
#include "avfilter_internal.h"
#include "video.h"
#include "tvai_data.h"
#include "tvai.h"
#include "tvai_messages.h"

int ff_tvai_checkDevice(char* deviceString, DeviceSetting* pDevice, AVFilterContext* ctx);
int ff_tvai_checkScale(char* modelName, int scale, AVFilterContext* ctx);
int ff_tvai_checkModel(char* modelName, ModelType modelType, AVFilterContext* ctx);
void ff_tvai_handleLogging(void);
int ff_tvai_prepareProcessorInfo(char *deviceString, VideoProcessorInfo* pProcessorInfo, ModelType modelType, AVFilterLink *pOutlink, 
        BasicProcessorInfo* pBasic, int procIndex, DictionaryItem *pParameters, int parameterCount);
void ff_tvai_prepareBufferInput(TVAIBuffer* ioBuffer, AVFrame *in);
/* pPool: optional slot for a pool of SDK page-locked (DMA-capable) host
 * buffers, lazily created on first use; pass NULL for regular allocation.
 * Owned by the caller, release with av_buffer_pool_uninit(). */
AVFrame* ff_tvai_prepareBufferOutput(AVFilterLink *outlink, TVAIBuffer* oBuffer, AVBufferPool **pPool);

/* Allocates an AVFrame backed by the SDK frame pool (tvai_alloc_buffer,
 * page-locked when CUDA is available), creating *pPool on first use.
 * Returns NULL on failure; the caller should fall back to default
 * allocation. Frame props are stamped from link like
 * ff_default_get_video_buffer does. */
AVFrame* ff_tvai_pool_frame(AVBufferPool **pPool, AVFilterLink *link, int format, int w, int h);

/* Pool of SDK page-locked input buffers, served through an input pad's
 * get_buffer.video hook so the upstream filter (typically hwdownload)
 * writes straight into DMA-capable memory. The pool is recreated when the
 * requested geometry/format changes; falls back to default allocation. */
typedef struct TVAIInPool {
    AVBufferPool *pool;
    int w, h, fmt;
} TVAIInPool;
AVFrame *ff_tvai_get_in_buffer(TVAIInPool *p, AVFilterLink *inlink, int w, int h);
void ff_tvai_in_pool_uninit(TVAIInPool *p);

int ff_tvai_add_output(void *pProcessor, AVFilterLink *outlink, AVFrame* frame, AVBufferPool **pPool);
int ff_tvai_process(void *pFrameProcessor, AVFrame* frame);
void ff_tvai_ignore_output(void *pProcessor);
void av_dict_set_float(AVDictionary **dict, const char *key, float value, int flag);
void ff_av_dict_log(AVFilterContext *ctx, const char* msg, const AVDictionary *dict);
DictionaryItem* ff_tvai_alloc_copy_entries(AVDictionary* dict, int *pCount);
int ff_tvai_copy_entries(AVDictionary* dict, DictionaryItem* pDictInfo);
int ff_tvai_postflight(AVFilterLink *outlink, void* pFrameProcessor, AVFrame* previousFrame, AVBufferPool **pPool);

#endif
