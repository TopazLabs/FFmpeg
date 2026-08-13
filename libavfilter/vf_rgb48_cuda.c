/*
 * CUDA-accelerated 8-bit YUV 4:2:0 <-> packed RGB48 conversion
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

#include <string.h>

#include "libavutil/common.h"
#include "libavutil/cuda_check.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_cuda_internal.h"
#include "libavutil/internal.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#include "avfilter.h"
#include "filters.h"

#include "cuda/load_helper.h"

#define DIV_UP(a, b) (((a) + (b) - 1) / (b))
#define BLOCKX 32
#define BLOCKY 16

#define CHECK_CU(x) FF_CUDA_CHECK_DL(ctx, s->hwctx->internal->cuda_dl, x)

enum ConvDirection {
    DIR_YUV2RGB,
    DIR_RGB2YUV,
};

enum Matrix {
    MATRIX_AUTO = -1,
    MATRIX_BT601,
    MATRIX_BT709,
    MATRIX_BT2020,
    MATRIX_NB,
};

typedef struct CUDARGB48Context {
    const AVClass *class;

    AVCUDADeviceContext *hwctx;
    AVBufferRef *frames_ctx;
    AVFrame *own_frame;
    AVFrame *tmp_frame;

    CUmodule cu_module;
    CUfunction cu_yuv2rgb48;
    CUfunction cu_yuv2rgbf32;
    CUfunction cu_rgb482yuv;
    CUfunction cu_rgbf322yuv;
    CUstream cu_stream;

    enum ConvDirection direction;
    enum AVPixelFormat in_fmt, out_fmt;

    enum AVPixelFormat format;  /* requested output sw format */
    int matrix;                 /* enum Matrix */
    int range;                  /* enum AVColorRange, output range for rgb->yuv */
} CUDARGB48Context;

static av_cold int cudargb48_init(AVFilterContext *ctx)
{
    CUDARGB48Context *s = ctx->priv;

    s->own_frame = av_frame_alloc();
    if (!s->own_frame)
        return AVERROR(ENOMEM);

    s->tmp_frame = av_frame_alloc();
    if (!s->tmp_frame)
        return AVERROR(ENOMEM);

    return 0;
}

static av_cold void cudargb48_uninit(AVFilterContext *ctx)
{
    CUDARGB48Context *s = ctx->priv;

    if (s->hwctx && s->cu_module) {
        CudaFunctions *cu = s->hwctx->internal->cuda_dl;
        CUcontext dummy;

        CHECK_CU(cu->cuCtxPushCurrent(s->hwctx->cuda_ctx));
        CHECK_CU(cu->cuModuleUnload(s->cu_module));
        s->cu_module = NULL;
        CHECK_CU(cu->cuCtxPopCurrent(&dummy));
    }

    av_frame_free(&s->own_frame);
    av_buffer_unref(&s->frames_ctx);
    av_frame_free(&s->tmp_frame);
}

static av_cold int init_hwframe_ctx(CUDARGB48Context *s, AVBufferRef *device_ctx,
                                    int width, int height)
{
    AVBufferRef *out_ref = NULL;
    AVHWFramesContext *out_ctx;
    int ret;

    out_ref = av_hwframe_ctx_alloc(device_ctx);
    if (!out_ref)
        return AVERROR(ENOMEM);

    out_ctx = (AVHWFramesContext *)out_ref->data;

    out_ctx->format    = AV_PIX_FMT_CUDA;
    out_ctx->sw_format = s->out_fmt;
    out_ctx->width     = FFALIGN(width, 32);
    out_ctx->height    = FFALIGN(height, 32);

    ret = av_hwframe_ctx_init(out_ref);
    if (ret < 0)
        goto fail;

    av_frame_unref(s->own_frame);
    ret = av_hwframe_get_buffer(out_ref, s->own_frame, 0);
    if (ret < 0)
        goto fail;

    s->own_frame->width  = width;
    s->own_frame->height = height;

    av_buffer_unref(&s->frames_ctx);
    s->frames_ctx = out_ref;

    return 0;
fail:
    av_buffer_unref(&out_ref);
    return ret;
}

static av_cold int init_processing_chain(AVFilterContext *ctx, int width,
                                         int height)
{
    FilterLink        *inl = ff_filter_link(ctx->inputs[0]);
    FilterLink       *outl = ff_filter_link(ctx->outputs[0]);
    CUDARGB48Context    *s = ctx->priv;
    AVHWFramesContext *in_frames_ctx;
    int ret;

    if (!inl->hw_frames_ctx) {
        av_log(ctx, AV_LOG_ERROR, "No hw context provided on input\n");
        return AVERROR(EINVAL);
    }

    in_frames_ctx = (AVHWFramesContext *)inl->hw_frames_ctx->data;
    s->in_fmt = in_frames_ctx->sw_format;

    switch (s->in_fmt) {
    case AV_PIX_FMT_NV12:
    case AV_PIX_FMT_YUV420P:
        s->direction = DIR_YUV2RGB;
        s->out_fmt   = s->format == AV_PIX_FMT_NONE ? AV_PIX_FMT_RGB48 : s->format;
        if (s->out_fmt != AV_PIX_FMT_RGB48 && s->out_fmt != AV_PIX_FMT_RGBF32 &&
            s->out_fmt != AV_PIX_FMT_RGBAF32) {
            av_log(ctx, AV_LOG_ERROR,
                   "YUV input can only be converted to rgb48, rgbf32 or rgbaf32\n");
            return AVERROR(EINVAL);
        }
        break;
    case AV_PIX_FMT_RGB48:
    case AV_PIX_FMT_RGBF32:
    case AV_PIX_FMT_RGBAF32:
        s->direction = DIR_RGB2YUV;
        s->out_fmt   = s->format == AV_PIX_FMT_NONE ? AV_PIX_FMT_NV12 : s->format;
        if (s->out_fmt != AV_PIX_FMT_NV12 && s->out_fmt != AV_PIX_FMT_YUV420P) {
            av_log(ctx, AV_LOG_ERROR,
                   "RGB input can only be converted to nv12 or yuv420p\n");
            return AVERROR(EINVAL);
        }
        break;
    default:
        av_log(ctx, AV_LOG_ERROR, "Unsupported input format: %s\n",
               av_get_pix_fmt_name(s->in_fmt));
        return AVERROR(EINVAL);
    }

    ret = init_hwframe_ctx(s, in_frames_ctx->device_ref, width, height);
    if (ret < 0)
        return ret;

    outl->hw_frames_ctx = av_buffer_ref(s->frames_ctx);
    if (!outl->hw_frames_ctx)
        return AVERROR(ENOMEM);

    return 0;
}

static av_cold int cudargb48_load_functions(AVFilterContext *ctx)
{
    CUDARGB48Context *s = ctx->priv;
    CUcontext dummy, cuda_ctx = s->hwctx->cuda_ctx;
    CudaFunctions *cu = s->hwctx->internal->cuda_dl;
    int ret;

    extern const unsigned char ff_vf_rgb48_cuda_ptx_data[];
    extern const unsigned int ff_vf_rgb48_cuda_ptx_len;

    ret = CHECK_CU(cu->cuCtxPushCurrent(cuda_ctx));
    if (ret < 0)
        return ret;

    ret = ff_cuda_load_module(ctx, s->hwctx, &s->cu_module,
                              ff_vf_rgb48_cuda_ptx_data,
                              ff_vf_rgb48_cuda_ptx_len);
    if (ret < 0)
        goto fail;

    ret = CHECK_CU(cu->cuModuleGetFunction(&s->cu_yuv2rgb48, s->cu_module,
                                           "yuv8_to_rgb48"));
    if (ret < 0)
        goto fail;

    ret = CHECK_CU(cu->cuModuleGetFunction(&s->cu_yuv2rgbf32, s->cu_module,
                                           "yuv8_to_rgbf32"));
    if (ret < 0)
        goto fail;

    ret = CHECK_CU(cu->cuModuleGetFunction(&s->cu_rgb482yuv, s->cu_module,
                                           "rgb48_to_yuv8"));
    if (ret < 0)
        goto fail;

    ret = CHECK_CU(cu->cuModuleGetFunction(&s->cu_rgbf322yuv, s->cu_module,
                                           "rgbf32_to_yuv8"));
    if (ret < 0)
        goto fail;

fail:
    CHECK_CU(cu->cuCtxPopCurrent(&dummy));
    return ret;
}

static av_cold int cudargb48_config_props(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink = outlink->src->inputs[0];
    FilterLink      *inl = ff_filter_link(inlink);
    CUDARGB48Context  *s = ctx->priv;
    AVHWFramesContext *frames_ctx;
    int ret;

    outlink->w = inlink->w;
    outlink->h = inlink->h;

    ret = init_processing_chain(ctx, inlink->w, inlink->h);
    if (ret < 0)
        return ret;

    frames_ctx = (AVHWFramesContext *)inl->hw_frames_ctx->data;
    s->hwctx = frames_ctx->device_ctx->hwctx;
    s->cu_stream = s->hwctx->stream;

    outlink->sample_aspect_ratio = inlink->sample_aspect_ratio;

    return cudargb48_load_functions(ctx);
}

static int pick_matrix(const CUDARGB48Context *s, const AVFrame *frame)
{
    if (s->matrix != MATRIX_AUTO)
        return s->matrix;

    switch (frame->colorspace) {
    case AVCOL_SPC_BT709:
        return MATRIX_BT709;
    case AVCOL_SPC_SMPTE170M:
    case AVCOL_SPC_BT470BG:
        return MATRIX_BT601;
    case AVCOL_SPC_BT2020_NCL:
    case AVCOL_SPC_BT2020_CL:
        return MATRIX_BT2020;
    default:
        /* same default as swscale: SD content is bt601, everything else bt709 */
        return (frame->width >= 1280 || frame->height > 576) ? MATRIX_BT709
                                                             : MATRIX_BT601;
    }
}

static const double kr_tab[MATRIX_NB] = { 0.299,  0.2126, 0.2627 };
static const double kb_tab[MATRIX_NB] = { 0.114,  0.0722, 0.0593 };

static const enum AVColorSpace spc_tab[MATRIX_NB] = {
    AVCOL_SPC_SMPTE170M, AVCOL_SPC_BT709, AVCOL_SPC_BT2020_NCL,
};

static int launch_yuv2rgb(AVFilterContext *ctx, AVFrame *out, AVFrame *in)
{
    CUDARGB48Context *s = ctx->priv;
    CudaFunctions *cu = s->hwctx->internal->cuda_dl;
    int matrix = pick_matrix(s, in);
    int full_range = in->color_range == AVCOL_RANGE_JPEG;
    double kr = kr_tab[matrix], kb = kb_tab[matrix], kg = 1.0 - kr - kb;

    float y_off   = full_range ? 0.0f : 16.0f;
    float y_scale = 1.0f / (full_range ? 255.0f : 219.0f);
    float c_scale = 1.0f / (full_range ? 255.0f : 224.0f);
    float crv = 2.0 * (1.0 - kr);
    float cbu = 2.0 * (1.0 - kb);
    float cgu = 2.0 * (1.0 - kb) * kb / kg;
    float cgv = 2.0 * (1.0 - kr) * kr / kg;

    int uv_step = s->in_fmt == AV_PIX_FMT_NV12 ? 2 : 1;
    const uint8_t *src_u = in->data[1];
    const uint8_t *src_v = uv_step == 2 ? in->data[1] + 1 : in->data[2];
    int pitch_u = in->linesize[1];
    int pitch_v = uv_step == 2 ? in->linesize[1] : in->linesize[2];
    int is_float  = s->out_fmt != AV_PIX_FMT_RGB48;
    int nch       = s->out_fmt == AV_PIX_FMT_RGBAF32 ? 4 : 3;
    int dst_pitch = out->linesize[0] / (is_float ? 4 : 2); /* in output elements */
    CUfunction fn = is_float ? s->cu_yuv2rgbf32 : s->cu_yuv2rgb48;

    void *args_rgb48[] = {
        &in->data[0], &in->linesize[0],
        &src_u, &pitch_u, &src_v, &pitch_v, &uv_step,
        &out->data[0], &dst_pitch,
        &in->width, &in->height,
        &y_off, &y_scale, &c_scale, &crv, &cgu, &cgv, &cbu,
    };
    void *args_f32[] = {
        &in->data[0], &in->linesize[0],
        &src_u, &pitch_u, &src_v, &pitch_v, &uv_step,
        &out->data[0], &dst_pitch, &nch,
        &in->width, &in->height,
        &y_off, &y_scale, &c_scale, &crv, &cgu, &cgv, &cbu,
    };
    void **args = is_float ? args_f32 : args_rgb48;

    /* Keep the source colorspace/range props on the RGB frames: RGB
     * processing ignores them, and they let a downstream rgb48_cuda
     * instance reconstruct YUV with the original matrix and range. */

    return CHECK_CU(cu->cuLaunchKernel(fn,
                                       DIV_UP(in->width, BLOCKX),
                                       DIV_UP(in->height, BLOCKY), 1,
                                       BLOCKX, BLOCKY, 1, 0, s->cu_stream,
                                       args, NULL));
}

static int launch_rgb2yuv(AVFilterContext *ctx, AVFrame *out, AVFrame *in)
{
    CUDARGB48Context *s = ctx->priv;
    CudaFunctions *cu = s->hwctx->internal->cuda_dl;
    int matrix = s->matrix == MATRIX_AUTO ? pick_matrix(s, in) : s->matrix;
    int full_range = s->range == AVCOL_RANGE_UNSPECIFIED
                         ? in->color_range == AVCOL_RANGE_JPEG
                         : s->range == AVCOL_RANGE_JPEG;
    double kr = kr_tab[matrix], kb = kb_tab[matrix], kg = 1.0 - kr - kb;

    float ry = kr, gy = kg, by = kb;
    float y_scale = full_range ? 255.0f : 219.0f;
    float y_off   = full_range ? 0.0f : 16.0f;
    float ru = -kr / (2.0 * (1.0 - kb)), gu = -kg / (2.0 * (1.0 - kb)), bu = 0.5f;
    float rv = 0.5f, gv = -kg / (2.0 * (1.0 - kr)), bv = -kb / (2.0 * (1.0 - kr));
    float c_scale = full_range ? 255.0f : 224.0f;

    int uv_step = s->out_fmt == AV_PIX_FMT_NV12 ? 2 : 1;
    uint8_t *dst_u = out->data[1];
    uint8_t *dst_v = uv_step == 2 ? out->data[1] + 1 : out->data[2];
    int pitch_u = out->linesize[1];
    int pitch_v = uv_step == 2 ? out->linesize[1] : out->linesize[2];
    int is_float  = s->in_fmt != AV_PIX_FMT_RGB48;
    int nch       = s->in_fmt == AV_PIX_FMT_RGBAF32 ? 4 : 3;
    int src_pitch = in->linesize[0] / (is_float ? 4 : 2); /* in input elements */
    CUfunction fn = is_float ? s->cu_rgbf322yuv : s->cu_rgb482yuv;

    void *args_rgb48[] = {
        &in->data[0], &src_pitch,
        &out->data[0], &out->linesize[0],
        &dst_u, &pitch_u, &dst_v, &pitch_v, &uv_step,
        &in->width, &in->height,
        &ry, &gy, &by, &y_off, &y_scale,
        &ru, &gu, &bu, &rv, &gv, &bv, &c_scale,
    };
    void *args_f32[] = {
        &in->data[0], &src_pitch, &nch,
        &out->data[0], &out->linesize[0],
        &dst_u, &pitch_u, &dst_v, &pitch_v, &uv_step,
        &in->width, &in->height,
        &ry, &gy, &by, &y_off, &y_scale,
        &ru, &gu, &bu, &rv, &gv, &bv, &c_scale,
    };
    void **args = is_float ? args_f32 : args_rgb48;

    out->colorspace      = spc_tab[matrix];
    out->color_range     = full_range ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;
    out->chroma_location = AVCHROMA_LOC_LEFT;

    return CHECK_CU(cu->cuLaunchKernel(fn,
                                       DIV_UP((in->width  + 1) / 2, BLOCKX),
                                       DIV_UP((in->height + 1) / 2, BLOCKY), 1,
                                       BLOCKX, BLOCKY, 1, 0, s->cu_stream,
                                       args, NULL));
}

static int cudargb48_conv(AVFilterContext *ctx, AVFrame *out, AVFrame *in)
{
    CUDARGB48Context *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    int ret;

    ret = av_frame_copy_props(s->own_frame, in);
    if (ret < 0)
        return ret;

    if (s->direction == DIR_YUV2RGB)
        ret = launch_yuv2rgb(ctx, s->own_frame, in);
    else
        ret = launch_rgb2yuv(ctx, s->own_frame, in);
    if (ret < 0)
        return ret;

    ret = av_hwframe_get_buffer(s->own_frame->hw_frames_ctx, s->tmp_frame, 0);
    if (ret < 0)
        return ret;

    av_frame_move_ref(out, s->own_frame);
    av_frame_move_ref(s->own_frame, s->tmp_frame);

    s->own_frame->width  = outlink->w;
    s->own_frame->height = outlink->h;

    return 0;
}

static int cudargb48_filter_frame(AVFilterLink *link, AVFrame *in)
{
    AVFilterContext *ctx = link->dst;
    CUDARGB48Context *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    CudaFunctions *cu = s->hwctx->internal->cuda_dl;

    AVFrame *out = NULL;
    CUcontext dummy;
    int ret = 0;

    out = av_frame_alloc();
    if (!out) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    ret = CHECK_CU(cu->cuCtxPushCurrent(s->hwctx->cuda_ctx));
    if (ret < 0)
        goto fail;

    ret = cudargb48_conv(ctx, out, in);

    CHECK_CU(cu->cuCtxPopCurrent(&dummy));
    if (ret < 0)
        goto fail;

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
fail:
    av_frame_free(&in);
    av_frame_free(&out);
    return ret;
}

#define OFFSET(x) offsetof(CUDARGB48Context, x)
#define FLAGS (AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)
static const AVOption rgb48_cuda_options[] = {
    { "format", "Output software pixel format", OFFSET(format), AV_OPT_TYPE_PIXEL_FMT,
        { .i64 = AV_PIX_FMT_NONE }, AV_PIX_FMT_NONE, INT_MAX, FLAGS },
    { "matrix", "YUV colorspace matrix", OFFSET(matrix), AV_OPT_TYPE_INT,
        { .i64 = MATRIX_AUTO }, MATRIX_AUTO, MATRIX_NB - 1, FLAGS, .unit = "matrix" },
        { "auto",   "deduce from frame/resolution", 0, AV_OPT_TYPE_CONST, { .i64 = MATRIX_AUTO   }, 0, 0, FLAGS, .unit = "matrix" },
        { "bt601",  "BT.601 / SMPTE 170M",          0, AV_OPT_TYPE_CONST, { .i64 = MATRIX_BT601  }, 0, 0, FLAGS, .unit = "matrix" },
        { "bt709",  "BT.709",                       0, AV_OPT_TYPE_CONST, { .i64 = MATRIX_BT709  }, 0, 0, FLAGS, .unit = "matrix" },
        { "bt2020", "BT.2020 (NCL)",                0, AV_OPT_TYPE_CONST, { .i64 = MATRIX_BT2020 }, 0, 0, FLAGS, .unit = "matrix" },
    { "range", "Output color range for RGB->YUV", OFFSET(range), AV_OPT_TYPE_INT,
        { .i64 = AVCOL_RANGE_UNSPECIFIED }, 0, AVCOL_RANGE_NB - 1, FLAGS, .unit = "range" },
        { "auto", "Preserve input range", 0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_RANGE_UNSPECIFIED }, 0, 0, FLAGS, .unit = "range" },
        { "tv",   "Limited range", 0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_RANGE_MPEG }, 0, 0, FLAGS, .unit = "range" },
        { "mpeg", "Limited range", 0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_RANGE_MPEG }, 0, 0, FLAGS, .unit = "range" },
        { "pc",   "Full range",    0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_RANGE_JPEG }, 0, 0, FLAGS, .unit = "range" },
        { "jpeg", "Full range",    0, AV_OPT_TYPE_CONST, { .i64 = AVCOL_RANGE_JPEG }, 0, 0, FLAGS, .unit = "range" },
    { NULL },
};

static const AVClass rgb48_cuda_class = {
    .class_name = "rgb48_cuda",
    .item_name  = av_default_item_name,
    .option     = rgb48_cuda_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const AVFilterPad rgb48_cuda_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = cudargb48_filter_frame,
    },
};

static const AVFilterPad rgb48_cuda_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = cudargb48_config_props,
    },
};

const FFFilter ff_vf_rgb48_cuda = {
    .p.name        = "rgb48_cuda",
    .p.description = NULL_IF_CONFIG_SMALL("CUDA accelerated YUV 4:2:0 <-> RGB48 conversion"),

    .p.priv_class  = &rgb48_cuda_class,

    .init   = cudargb48_init,
    .uninit = cudargb48_uninit,

    .priv_size = sizeof(CUDARGB48Context),

    FILTER_INPUTS(rgb48_cuda_inputs),
    FILTER_OUTPUTS(rgb48_cuda_outputs),

    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_CUDA),

    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
