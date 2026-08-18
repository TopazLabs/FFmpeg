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

// Whether frames should be handed to the library in the memory of a GPU rather than the host's,
// which TVAI_USE_GPU=1 asks for. A filter set up that way takes and produces AV_PIX_FMT_CUDA frames
// and the pixels never come across the bus. It says nothing about where the model runs or where the
// library keeps its own frames, which are the device and parameters=frameStorage=cuda options; all
// four combinations of the two work.
int ff_tvai_device_frames(void);

// The formats a tvai filter takes: `format` in host memory, plus CUDA frames when they have been
// asked for and this build can deal with them.
int ff_tvai_query_formats(const AVFilterContext *ctx, AVFilterFormatsConfig **cfg_in,
        AVFilterFormatsConfig **cfg_out, enum AVPixelFormat format);

int ff_tvai_checkDevice(char* deviceString, DeviceSetting* pDevice, AVFilterContext* ctx);
int ff_tvai_checkScale(char* modelName, int scale, AVFilterContext* ctx);
int ff_tvai_checkModel(char* modelName, ModelType modelType, AVFilterContext* ctx);
void ff_tvai_handleLogging(void);
int ff_tvai_prepareProcessorInfo(char *deviceString, VideoProcessorInfo* pProcessorInfo, ModelType modelType, AVFilterLink *pOutlink, 
        BasicProcessorInfo* pBasic, int procIndex, DictionaryItem *pParameters, int parameterCount);
void ff_tvai_prepareBufferInput(TVAIBuffer* ioBuffer, AVFrame *in);
AVFrame* ff_tvai_prepareBufferOutput(AVFilterLink *outlink, TVAIBuffer* oBuffer);

int ff_tvai_add_output(void *pProcessor, AVFilterLink *outlink, AVFrame* frame);
int ff_tvai_process(void *pFrameProcessor, AVFrame* frame);
// Hands a frame over timed by `pts` and `duration` instead of its own, which frame interpolation
// needs because it works in a time base of its own.
int ff_tvai_process_timed(void *pFrameProcessor, AVFrame* frame, long long pts, long long duration);
void ff_tvai_ignore_output(void *pProcessor);
void av_dict_set_float(AVDictionary **dict, const char *key, float value, int flag);
void ff_av_dict_log(AVFilterContext *ctx, const char* msg, const AVDictionary *dict);
DictionaryItem* ff_tvai_alloc_copy_entries(AVDictionary* dict, int *pCount);
int ff_tvai_copy_entries(AVDictionary* dict, DictionaryItem* pDictInfo);
int ff_tvai_postflight(AVFilterLink *outlink, void* pFrameProcessor, AVFrame* previousFrame);

#endif
