/*
 * VideoToolbox H.264 standalone hardware decoder
 *
 * This is a standalone decoder that does NOT depend on the software h264
 * decoder. It uses h264_parser for SPS/PPS extraction and feeds compressed
 * bitstream directly to VideoToolbox's VTDecompressionSession.
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

#include "config_components.h"

#define Picture QuickdrawPicture
#include <VideoToolbox/VideoToolbox.h>
#undef Picture

#include "libavutil/avutil.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_videotoolbox.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#include "avcodec.h"
#include "codec_internal.h"
#include "decode.h"
#include "h264_parse.h"
#include "h264_ps.h"
#include "hwconfig.h"
#include "internal.h"

#ifndef kVTVideoDecoderSpecification_RequireHardwareAcceleratedVideoDecoder
#define kVTVideoDecoderSpecification_RequireHardwareAcceleratedVideoDecoder \
    CFSTR("RequireHardwareAcceleratedVideoDecoder")
#endif
#ifndef kVTVideoDecoderSpecification_EnableHardwareAcceleratedVideoDecoder
#define kVTVideoDecoderSpecification_EnableHardwareAcceleratedVideoDecoder \
    CFSTR("EnableHardwareAcceleratedVideoDecoder")
#endif

typedef struct VTDecContext {
    AVClass *avclass;

    /* VideoToolbox session state */
    VTDecompressionSessionRef   session;
    CMVideoFormatDescriptionRef cm_fmt_desc;

    /* Decoded frame from VT callback */
    CVPixelBufferRef            decoded_frame;

    /* H.264 parameter sets for extradata parsing */
    H264ParamSets ps;
    int           is_avc;
    int           nal_length_size;

    /* Hardware frames context */
    AVBufferRef *cached_hw_frames_ctx;

    /* SPS profile/compat/level for reconfig detection */
    uint8_t sps_header[3];

    /* Options */
    int require_hw;
} VTDecContext;

static void vtdec_decoder_callback(void *opaque,
                                   void *sourceFrameRefCon,
                                   OSStatus status,
                                   VTDecodeInfoFlags flags,
                                   CVImageBufferRef image_buffer,
                                   CMTime pts,
                                   CMTime duration)
{
    VTDecContext *ctx = opaque;

    if (ctx->decoded_frame) {
        CVPixelBufferRelease(ctx->decoded_frame);
        ctx->decoded_frame = NULL;
    }

    if (!image_buffer) {
        av_log(ctx, AV_LOG_WARNING,
               "VideoToolbox decoder callback: no image buffer (status=%d)\n",
               (int)status);
        return;
    }

    ctx->decoded_frame = CVPixelBufferRetain(image_buffer);
}

/* Create CMVideoFormatDescription from avcC extradata */
static int vtdec_create_fmt_desc_avcc(AVCodecContext *avctx)
{
    VTDecContext *ctx = avctx->priv_data;
    CFMutableDictionaryRef decoder_spec;
    CFMutableDictionaryRef atoms;
    CFDataRef avcc_data;
    OSStatus status;

    decoder_spec = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
                                             &kCFTypeDictionaryKeyCallBacks,
                                             &kCFTypeDictionaryValueCallBacks);
    if (!decoder_spec)
        return AVERROR(ENOMEM);

    CFDictionarySetValue(decoder_spec,
                         ctx->require_hw
                             ? kVTVideoDecoderSpecification_RequireHardwareAcceleratedVideoDecoder
                             : kVTVideoDecoderSpecification_EnableHardwareAcceleratedVideoDecoder,
                         kCFBooleanTrue);

    atoms = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
                                      &kCFTypeDictionaryKeyCallBacks,
                                      &kCFTypeDictionaryValueCallBacks);
    if (!atoms) {
        CFRelease(decoder_spec);
        return AVERROR(ENOMEM);
    }

    avcc_data = CFDataCreate(kCFAllocatorDefault,
                             avctx->extradata, avctx->extradata_size);
    if (!avcc_data) {
        CFRelease(atoms);
        CFRelease(decoder_spec);
        return AVERROR(ENOMEM);
    }

    CFDictionarySetValue(atoms, CFSTR("avcC"), avcc_data);
    CFRelease(avcc_data);

    CFDictionarySetValue(decoder_spec,
                         kCMFormatDescriptionExtension_SampleDescriptionExtensionAtoms,
                         atoms);
    CFRelease(atoms);

    status = CMVideoFormatDescriptionCreate(kCFAllocatorDefault,
                                            kCMVideoCodecType_H264,
                                            avctx->width,
                                            avctx->height,
                                            decoder_spec,
                                            &ctx->cm_fmt_desc);
    CFRelease(decoder_spec);

    if (status != noErr) {
        av_log(avctx, AV_LOG_ERROR,
               "Failed to create CMVideoFormatDescription from avcC: %d\n",
               (int)status);
        return AVERROR_EXTERNAL;
    }

    return 0;
}

/* Find NAL unit boundaries in Annex B extradata.
 * Returns pointers to NAL units (past start code) and their sizes. */
static int vtdec_find_annexb_nalus(const uint8_t *data, int size,
                                   const uint8_t **nalus, size_t *nalu_sizes,
                                   int max_nalus, int *nb_nalus)
{
    int i = 0;
    *nb_nalus = 0;

    while (i < size) {
        /* Find start code: 0x000001 or 0x00000001 */
        if (i + 2 < size && data[i] == 0 && data[i + 1] == 0) {
            int sc_len;
            if (data[i + 2] == 1) {
                sc_len = 3;
            } else if (i + 3 < size && data[i + 2] == 0 && data[i + 3] == 1) {
                sc_len = 4;
            } else {
                i++;
                continue;
            }

            int nalu_start = i + sc_len;
            /* Find end of this NALU (next start code or end of data) */
            int nalu_end = size;
            for (int j = nalu_start + 1; j + 2 < size; j++) {
                if (data[j] == 0 && data[j + 1] == 0 &&
                    (data[j + 2] == 1 || (j + 3 < size && data[j + 2] == 0 && data[j + 3] == 1))) {
                    nalu_end = j;
                    break;
                }
            }

            /* Remove trailing zeros */
            while (nalu_end > nalu_start && data[nalu_end - 1] == 0)
                nalu_end--;

            if (nalu_end > nalu_start && *nb_nalus < max_nalus) {
                nalus[*nb_nalus] = data + nalu_start;
                nalu_sizes[*nb_nalus] = nalu_end - nalu_start;
                (*nb_nalus)++;
            }

            i = nalu_end;
        } else {
            i++;
        }
    }

    return 0;
}

/* Create CMVideoFormatDescription from Annex B extradata */
static int vtdec_create_fmt_desc_annexb(AVCodecContext *avctx)
{
    VTDecContext *ctx = avctx->priv_data;
    const uint8_t *nalus[32];
    size_t nalu_sizes[32];
    int nb_nalus = 0;
    const uint8_t *sps_list[16];
    size_t sps_sizes[16];
    const uint8_t *pps_list[16];
    size_t pps_sizes[16];
    int nb_sps = 0, nb_pps = 0;
    int nb_ps;
    const uint8_t **ps_array;
    size_t *ps_sizes;
    OSStatus status;

    vtdec_find_annexb_nalus(avctx->extradata, avctx->extradata_size,
                            nalus, nalu_sizes, 32, &nb_nalus);

    for (int i = 0; i < nb_nalus; i++) {
        uint8_t nal_type = nalus[i][0] & 0x1F;
        if (nal_type == 7 && nb_sps < 16) { /* SPS */
            sps_list[nb_sps] = nalus[i];
            sps_sizes[nb_sps] = nalu_sizes[i];
            nb_sps++;
        } else if (nal_type == 8 && nb_pps < 16) { /* PPS */
            pps_list[nb_pps] = nalus[i];
            pps_sizes[nb_pps] = nalu_sizes[i];
            nb_pps++;
        }
    }

    if (nb_sps == 0 || nb_pps == 0) {
        av_log(avctx, AV_LOG_ERROR,
               "No SPS/PPS found in Annex B extradata\n");
        return AVERROR_INVALIDDATA;
    }

    nb_ps = nb_sps + nb_pps;
    ps_array = av_malloc_array(nb_ps, sizeof(*ps_array));
    ps_sizes = av_malloc_array(nb_ps, sizeof(*ps_sizes));
    if (!ps_array || !ps_sizes) {
        av_free(ps_array);
        av_free(ps_sizes);
        return AVERROR(ENOMEM);
    }

    for (int i = 0; i < nb_sps; i++) {
        ps_array[i] = sps_list[i];
        ps_sizes[i] = sps_sizes[i];
    }
    for (int i = 0; i < nb_pps; i++) {
        ps_array[nb_sps + i] = pps_list[i];
        ps_sizes[nb_sps + i] = pps_sizes[i];
    }

    status = CMVideoFormatDescriptionCreateFromH264ParameterSets(
                 kCFAllocatorDefault,
                 nb_ps,
                 ps_array,
                 ps_sizes,
                 4, /* NAL length size */
                 &ctx->cm_fmt_desc);

    av_free(ps_array);
    av_free(ps_sizes);

    if (status != noErr) {
        av_log(avctx, AV_LOG_ERROR,
               "Failed to create CMVideoFormatDescription from Annex B: %d\n",
               (int)status);
        return AVERROR_EXTERNAL;
    }

    return 0;
}

static int vtdec_create_session(AVCodecContext *avctx)
{
    VTDecContext *ctx = avctx->priv_data;
    OSStatus status;
    VTDecompressionOutputCallbackRecord decoder_cb;
    CFMutableDictionaryRef buf_attr;
    CFMutableDictionaryRef io_surface_props;
    CFNumberRef cv_pix_fmt;
    CFNumberRef w, h;
    CFMutableDictionaryRef decoder_spec;
    int width  = avctx->width;
    int height = avctx->height;
    OSType pix_fmt = kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;

    /* Build destination buffer attributes */
    w = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &width);
    h = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &height);
    cv_pix_fmt = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &pix_fmt);

    buf_attr = CFDictionaryCreateMutable(kCFAllocatorDefault, 4,
                                         &kCFTypeDictionaryKeyCallBacks,
                                         &kCFTypeDictionaryValueCallBacks);
    io_surface_props = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
                                                 &kCFTypeDictionaryKeyCallBacks,
                                                 &kCFTypeDictionaryValueCallBacks);

    CFDictionarySetValue(buf_attr, kCVPixelBufferPixelFormatTypeKey, cv_pix_fmt);
    CFDictionarySetValue(buf_attr, kCVPixelBufferIOSurfacePropertiesKey, io_surface_props);
    CFDictionarySetValue(buf_attr, kCVPixelBufferWidthKey, w);
    CFDictionarySetValue(buf_attr, kCVPixelBufferHeightKey, h);
#if TARGET_OS_IPHONE
    CFDictionarySetValue(buf_attr, kCVPixelBufferOpenGLESCompatibilityKey, kCFBooleanTrue);
#else
    CFDictionarySetValue(buf_attr, kCVPixelBufferIOSurfaceOpenGLTextureCompatibilityKey, kCFBooleanTrue);
#endif

    CFRelease(io_surface_props);
    CFRelease(cv_pix_fmt);
    CFRelease(w);
    CFRelease(h);

    /* Build decoder specification */
    decoder_spec = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
                                             &kCFTypeDictionaryKeyCallBacks,
                                             &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(decoder_spec,
                         ctx->require_hw
                             ? kVTVideoDecoderSpecification_RequireHardwareAcceleratedVideoDecoder
                             : kVTVideoDecoderSpecification_EnableHardwareAcceleratedVideoDecoder,
                         kCFBooleanTrue);

#if defined(MAC_OS_VERSION_11_0) && !TARGET_OS_IPHONE && \
    (MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_VERSION_11_0) && AV_HAS_BUILTIN(__builtin_available)
    if (__builtin_available(macOS 11.0, *))
        VTRegisterSupplementalVideoDecoderIfAvailable(kCMVideoCodecType_H264);
#endif

    decoder_cb.decompressionOutputCallback = vtdec_decoder_callback;
    decoder_cb.decompressionOutputRefCon   = ctx;

    status = VTDecompressionSessionCreate(NULL,
                                          ctx->cm_fmt_desc,
                                          decoder_spec,
                                          buf_attr,
                                          &decoder_cb,
                                          &ctx->session);

    CFRelease(decoder_spec);
    CFRelease(buf_attr);

    if (status != noErr) {
        av_log(avctx, AV_LOG_ERROR,
               "Failed to create VTDecompressionSession: %d\n", (int)status);
        return AVERROR_EXTERNAL;
    }

    return 0;
}

static void vtdec_destroy_session(VTDecContext *ctx)
{
    if (ctx->session) {
        VTDecompressionSessionInvalidate(ctx->session);
        CFRelease(ctx->session);
        ctx->session = NULL;
    }

    if (ctx->cm_fmt_desc) {
        CFRelease(ctx->cm_fmt_desc);
        ctx->cm_fmt_desc = NULL;
    }
}

static int vtdec_init_hw_frames_ctx(AVCodecContext *avctx)
{
    VTDecContext *ctx = avctx->priv_data;
    AVBufferRef *device_ref = avctx->hw_device_ctx;
    AVBufferRef *hw_frames_ref;
    AVHWFramesContext *hw_frames;
    int ret;

    if (avctx->hw_frames_ctx) {
        ctx->cached_hw_frames_ctx = av_buffer_ref(avctx->hw_frames_ctx);
        if (!ctx->cached_hw_frames_ctx)
            return AVERROR(ENOMEM);
        return 0;
    }

    if (!device_ref) {
        ret = av_hwdevice_ctx_create(&device_ref, AV_HWDEVICE_TYPE_VIDEOTOOLBOX,
                                     NULL, NULL, 0);
        if (ret < 0)
            return ret;
    } else {
        device_ref = av_buffer_ref(device_ref);
        if (!device_ref)
            return AVERROR(ENOMEM);
    }

    hw_frames_ref = av_hwframe_ctx_alloc(device_ref);
    av_buffer_unref(&device_ref);
    if (!hw_frames_ref)
        return AVERROR(ENOMEM);

    hw_frames = (AVHWFramesContext *)hw_frames_ref->data;
    hw_frames->format    = AV_PIX_FMT_VIDEOTOOLBOX;
    hw_frames->sw_format = AV_PIX_FMT_NV12;
    hw_frames->width     = avctx->width;
    hw_frames->height    = avctx->height;

    ret = av_hwframe_ctx_init(hw_frames_ref);
    if (ret < 0) {
        av_buffer_unref(&hw_frames_ref);
        return ret;
    }

    avctx->hw_frames_ctx = av_buffer_ref(hw_frames_ref);
    if (!avctx->hw_frames_ctx) {
        av_buffer_unref(&hw_frames_ref);
        return AVERROR(ENOMEM);
    }

    ctx->cached_hw_frames_ctx = hw_frames_ref;
    return 0;
}

static av_cold int vtdec_init(AVCodecContext *avctx)
{
    VTDecContext *ctx = avctx->priv_data;
    int ret;

    if (!avctx->extradata || avctx->extradata_size < 7) {
        av_log(avctx, AV_LOG_ERROR,
               "h264_videotoolbox decoder requires extradata\n");
        return AVERROR_INVALIDDATA;
    }

    /* Parse extradata to determine format and extract SPS/PPS info */
    ret = ff_h264_decode_extradata(avctx->extradata, avctx->extradata_size,
                                   &ctx->ps, &ctx->is_avc,
                                   &ctx->nal_length_size, 0, avctx);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to parse H.264 extradata\n");
        return ret;
    }

    /* Set dimensions from SPS if not already set */
    if (ctx->ps.sps && (!avctx->width || !avctx->height)) {
        avctx->width  = 16 * ctx->ps.sps->mb_width;
        avctx->height = 16 * ctx->ps.sps->mb_height;
        if (ctx->ps.sps->frame_mbs_only_flag == 0)
            avctx->height *= 2;

        if (ctx->ps.sps->crop) {
            avctx->width  -= (ctx->ps.sps->crop_left + ctx->ps.sps->crop_right);
            avctx->height -= (ctx->ps.sps->crop_top + ctx->ps.sps->crop_bottom);
        }
    }

    if (ctx->ps.sps) {
        avctx->profile = ff_h264_get_profile(ctx->ps.sps);
        avctx->level   = ctx->ps.sps->level_idc;
        memcpy(ctx->sps_header, ctx->ps.sps->data + 1, 3);
    }

    avctx->pix_fmt = AV_PIX_FMT_VIDEOTOOLBOX;

    /* Create CMVideoFormatDescription */
    if (ctx->is_avc)
        ret = vtdec_create_fmt_desc_avcc(avctx);
    else
        ret = vtdec_create_fmt_desc_annexb(avctx);
    if (ret < 0)
        return ret;

    /* Set up hardware frames context */
    ret = vtdec_init_hw_frames_ctx(avctx);
    if (ret < 0)
        return ret;

    /* Create VTDecompressionSession */
    ret = vtdec_create_session(avctx);
    if (ret < 0)
        return ret;

    return 0;
}

/* Convert Annex B start-code-prefixed NALUs to length-prefixed format */
static int vtdec_annexb_to_mp4(const uint8_t *data, int size,
                               uint8_t **out, int *out_size)
{
    const uint8_t *p = data;
    const uint8_t *end = data + size;
    int total_size = 0;
    uint8_t *dst;

    /* First pass: compute output size */
    while (p < end) {
        if (p + 2 < end && p[0] == 0 && p[1] == 0) {
            int sc_len;
            if (p[2] == 1) {
                sc_len = 3;
            } else if (p + 3 < end && p[2] == 0 && p[3] == 1) {
                sc_len = 4;
            } else {
                p++;
                continue;
            }

            const uint8_t *nalu_start = p + sc_len;
            const uint8_t *nalu_end = end;
            for (const uint8_t *q = nalu_start + 1; q + 2 < end; q++) {
                if (q[0] == 0 && q[1] == 0 &&
                    (q[2] == 1 || (q + 3 < end && q[2] == 0 && q[3] == 1))) {
                    nalu_end = q;
                    break;
                }
            }

            /* Remove trailing zeros */
            while (nalu_end > nalu_start && nalu_end[-1] == 0)
                nalu_end--;

            total_size += 4 + (int)(nalu_end - nalu_start);
            p = nalu_end;
        } else {
            p++;
        }
    }

    if (total_size == 0)
        return AVERROR_INVALIDDATA;

    dst = av_malloc(total_size);
    if (!dst)
        return AVERROR(ENOMEM);

    *out = dst;
    *out_size = total_size;

    /* Second pass: write output */
    p = data;
    while (p < end) {
        if (p + 2 < end && p[0] == 0 && p[1] == 0) {
            int sc_len;
            if (p[2] == 1) {
                sc_len = 3;
            } else if (p + 3 < end && p[2] == 0 && p[3] == 1) {
                sc_len = 4;
            } else {
                p++;
                continue;
            }

            const uint8_t *nalu_start = p + sc_len;
            const uint8_t *nalu_end = end;
            for (const uint8_t *q = nalu_start + 1; q + 2 < end; q++) {
                if (q[0] == 0 && q[1] == 0 &&
                    (q[2] == 1 || (q + 3 < end && q[2] == 0 && q[3] == 1))) {
                    nalu_end = q;
                    break;
                }
            }

            while (nalu_end > nalu_start && nalu_end[-1] == 0)
                nalu_end--;

            int nalu_size = (int)(nalu_end - nalu_start);
            AV_WB32(dst, nalu_size);
            memcpy(dst + 4, nalu_start, nalu_size);
            dst += 4 + nalu_size;
            p = nalu_end;
        } else {
            p++;
        }
    }

    return 0;
}

/* Convert avcC NALUs from nal_length_size-byte length prefix to 4-byte */
static int vtdec_normalize_avcc(const uint8_t *data, int size,
                                int nal_length_size,
                                uint8_t **out, int *out_size)
{
    const uint8_t *p = data;
    const uint8_t *end = data + size;
    int total_size = 0;
    uint8_t *dst;

    /* First pass: compute output size */
    while (p + nal_length_size <= end) {
        int nalu_size = 0;
        for (int i = 0; i < nal_length_size; i++)
            nalu_size = (nalu_size << 8) | p[i];

        if (nalu_size <= 0 || p + nal_length_size + nalu_size > end)
            break;

        total_size += 4 + nalu_size;
        p += nal_length_size + nalu_size;
    }

    if (total_size == 0)
        return AVERROR_INVALIDDATA;

    dst = av_malloc(total_size);
    if (!dst)
        return AVERROR(ENOMEM);

    *out = dst;
    *out_size = total_size;

    /* Second pass: write output */
    p = data;
    while (p + nal_length_size <= end) {
        int nalu_size = 0;
        for (int i = 0; i < nal_length_size; i++)
            nalu_size = (nalu_size << 8) | p[i];

        if (nalu_size <= 0 || p + nal_length_size + nalu_size > end)
            break;

        AV_WB32(dst, nalu_size);
        memcpy(dst + 4, p + nal_length_size, nalu_size);
        dst += 4 + nalu_size;
        p += nal_length_size + nalu_size;
    }

    return 0;
}

static int vtdec_decode_packet(AVCodecContext *avctx, const AVPacket *pkt)
{
    VTDecContext *ctx = avctx->priv_data;
    CMSampleBufferRef sample_buf = NULL;
    CMBlockBufferRef block_buf = NULL;
    OSStatus status;
    uint8_t *converted = NULL;
    const uint8_t *send_data;
    int send_size;
    int ret = 0;

    /* Prepare packet data in length-prefixed format */
    if (ctx->is_avc && ctx->nal_length_size == 4) {
        /* Already 4-byte length prefixed, pass directly */
        send_data = pkt->data;
        send_size = pkt->size;
    } else if (ctx->is_avc) {
        /* Convert from nal_length_size-byte to 4-byte length prefix */
        ret = vtdec_normalize_avcc(pkt->data, pkt->size,
                                   ctx->nal_length_size,
                                   &converted, &send_size);
        if (ret < 0)
            return ret;
        send_data = converted;
    } else {
        /* Convert Annex B to length-prefixed */
        ret = vtdec_annexb_to_mp4(pkt->data, pkt->size,
                                  &converted, &send_size);
        if (ret < 0)
            return ret;
        send_data = converted;
    }

    /* Create CMBlockBuffer */
    status = CMBlockBufferCreateWithMemoryBlock(kCFAllocatorDefault,
                                                (void *)send_data,
                                                send_size,
                                                kCFAllocatorNull,
                                                NULL, 0, send_size, 0,
                                                &block_buf);
    if (status != noErr) {
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

    /* Create CMSampleBuffer */
    status = CMSampleBufferCreate(kCFAllocatorDefault,
                                  block_buf, TRUE,
                                  0, 0,
                                  ctx->cm_fmt_desc,
                                  1, 0, NULL, 0, NULL,
                                  &sample_buf);
    if (status != noErr) {
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

    /* Decode */
    status = VTDecompressionSessionDecodeFrame(ctx->session,
                                               sample_buf,
                                               0, NULL, 0);
    if (status != noErr) {
        av_log(avctx, AV_LOG_ERROR,
               "VTDecompressionSessionDecodeFrame failed: %d\n", (int)status);
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

    status = VTDecompressionSessionWaitForAsynchronousFrames(ctx->session);
    if (status != noErr) {
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

fail:
    if (sample_buf)
        CFRelease(sample_buf);
    if (block_buf)
        CFRelease(block_buf);
    av_free(converted);

    return ret;
}

static int vtdec_receive_frame(AVCodecContext *avctx, AVFrame *frame)
{
    VTDecContext *ctx = avctx->priv_data;
    AVPacket pkt;
    int ret;

    /* Get a packet from the decoder framework */
    ret = ff_decode_get_packet(avctx, &pkt);
    if (ret == AVERROR_EOF) {
        /* Flush: drain remaining frames */
        if (ctx->session)
            VTDecompressionSessionWaitForAsynchronousFrames(ctx->session);
        if (!ctx->decoded_frame)
            return AVERROR_EOF;
    } else if (ret < 0) {
        return ret;
    } else {
        /* Decode the packet */
        ret = vtdec_decode_packet(avctx, &pkt);
        av_packet_unref(&pkt);
        if (ret < 0)
            return ret;
    }

    /* Check if we got a decoded frame */
    if (!ctx->decoded_frame)
        return AVERROR(EAGAIN);

    /* Set up the output frame */
    frame->format = AV_PIX_FMT_VIDEOTOOLBOX;
    frame->width  = avctx->width;
    frame->height = avctx->height;
    frame->data[3] = (uint8_t *)ctx->decoded_frame;
    frame->buf[0] = av_buffer_create((uint8_t *)ctx->decoded_frame,
                                     sizeof(CVPixelBufferRef),
                                     (void (*)(void *, uint8_t *))CVPixelBufferRelease,
                                     NULL, 0);
    if (!frame->buf[0]) {
        CVPixelBufferRelease(ctx->decoded_frame);
        ctx->decoded_frame = NULL;
        return AVERROR(ENOMEM);
    }
    ctx->decoded_frame = NULL; /* ownership transferred to frame */

    if (ctx->cached_hw_frames_ctx) {
        frame->hw_frames_ctx = av_buffer_ref(ctx->cached_hw_frames_ctx);
        if (!frame->hw_frames_ctx)
            return AVERROR(ENOMEM);
    }

    return 0;
}

static av_cold int vtdec_close(AVCodecContext *avctx)
{
    VTDecContext *ctx = avctx->priv_data;

    vtdec_destroy_session(ctx);

    if (ctx->decoded_frame) {
        CVPixelBufferRelease(ctx->decoded_frame);
        ctx->decoded_frame = NULL;
    }

    av_buffer_unref(&ctx->cached_hw_frames_ctx);
    ff_h264_ps_uninit(&ctx->ps);

    return 0;
}

static void vtdec_flush(AVCodecContext *avctx)
{
    VTDecContext *ctx = avctx->priv_data;

    if (ctx->decoded_frame) {
        CVPixelBufferRelease(ctx->decoded_frame);
        ctx->decoded_frame = NULL;
    }

    /* Recreate session to flush VT's internal state */
    if (ctx->session) {
        VTDecompressionSessionInvalidate(ctx->session);
        CFRelease(ctx->session);
        ctx->session = NULL;
        vtdec_create_session(avctx);
    }
}

static const AVCodecHWConfigInternal *const vtdec_hw_configs[] = {
    &(const AVCodecHWConfigInternal) {
        .public = {
            .pix_fmt     = AV_PIX_FMT_VIDEOTOOLBOX,
            .methods     = AV_CODEC_HW_CONFIG_METHOD_AD_HOC |
                           AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX,
            .device_type = AV_HWDEVICE_TYPE_VIDEOTOOLBOX,
        },
        .hwaccel = NULL,
    },
    NULL
};

#define OFFSET(x) offsetof(VTDecContext, x)
#define VD AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_DECODING_PARAM
static const AVOption vtdec_options[] = {
    { "require_hw", "Require hardware acceleration (fail if unavailable)",
      OFFSET(require_hw), AV_OPT_TYPE_BOOL, {.i64 = 1}, 0, 1, VD },
    { NULL },
};

static const AVClass vtdec_class = {
    .class_name = "h264_videotoolbox",
    .item_name  = av_default_item_name,
    .option     = vtdec_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_h264_videotoolbox_decoder = {
    .p.name         = "h264_videotoolbox",
    CODEC_LONG_NAME("H.264 VideoToolbox Decoder"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_H264,
    .p.priv_class   = &vtdec_class,
    .priv_data_size = sizeof(VTDecContext),
    .init           = vtdec_init,
    FF_CODEC_RECEIVE_FRAME_CB(vtdec_receive_frame),
    .flush          = vtdec_flush,
    .close          = vtdec_close,
    .p.capabilities = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_AVOID_PROBING | AV_CODEC_CAP_HARDWARE,
    .caps_internal  = FF_CODEC_CAP_NOT_INIT_THREADSAFE,
    .hw_configs     = vtdec_hw_configs,
    .p.wrapper_name = "videotoolbox",
};
