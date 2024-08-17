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

int ff_tvai_checkDevice(int deviceIndex, AVFilterContext* ctx);
int ff_tvai_checkScale(char* modelName, int scale, AVFilterContext* ctx);
int ff_tvai_checkModel(char* modelName, ModelType modelType, AVFilterContext* ctx);
void ff_tvai_handleLogging(void);
int ff_tvai_prepareProcessorInfo(VideoProcessorInfo* pProcessorInfo, ModelType modelType, AVFilterLink *pOutlink, 
        BasicProcessorInfo* pBasic, int procIndex, DictionaryItem *pParameters, int parameterCount);
void ff_tvai_prepareBufferInput(TVAIBuffer* ioBuffer, AVFrame *in);
AVFrame* ff_tvai_prepareBufferOutput(AVFilterLink *outlink, TVAIBuffer* oBuffer);

int ff_tvai_add_output(void *pProcessor, AVFilterLink *outlink, AVFrame* frame);
int ff_tvai_process(void *pFrameProcessor, AVFrame* frame);
void ff_tvai_ignore_output(void *pProcessor);
void av_dict_set_float(AVDictionary **dict, const char *key, float value, int flag);
void ff_av_dict_log(AVFilterContext *ctx, const char* msg, const AVDictionary *dict);
DictionaryItem* ff_tvai_alloc_copy_entries(AVDictionary* dict, int *pCount);
int ff_tvai_copy_entries(AVDictionary* dict, DictionaryItem* pDictInfo);
int ff_tvai_postflight(AVFilterLink *outlink, void* pFrameProcessor, AVFrame* previousFrame);

#endif
