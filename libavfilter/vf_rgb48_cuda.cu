/*
 * CUDA kernels for 8-bit YUV 4:2:0 <-> packed RGB48 conversion
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

extern "C" {

__device__ static inline float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

__device__ static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/*
 * Bilinear chroma fetch for one half-res plane at full-res position (x, y),
 * using MPEG-2/H.264 4:2:0 siting: horizontally co-sited with even luma
 * columns, vertically centered between luma rows.
 */
__device__ static inline float chroma_fetch(const unsigned char *p, int pitch,
                                            int step, int cw, int ch,
                                            int x, int y)
{
    int cx0 = x >> 1;
    int cx1 = min(cx0 + 1, cw - 1);
    float wx1 = (x & 1) ? 0.5f : 0.0f;

    /* chroma row c sits at luma row 2c + 0.5 -> pos = y*0.5 - 0.25 */
    int   cy0, cy1;
    float wy1;
    if (y & 1) {
        cy0 = y >> 1;
        cy1 = min(cy0 + 1, ch - 1);
        wy1 = 0.25f;
    } else {
        cy1 = y >> 1;
        cy0 = max(cy1 - 1, 0);
        wy1 = 0.75f;
    }

    float r0 = (1.0f - wx1) * p[cy0 * pitch + cx0 * step] +
                       wx1  * p[cy0 * pitch + cx1 * step];
    float r1 = (1.0f - wx1) * p[cy1 * pitch + cx0 * step] +
                       wx1  * p[cy1 * pitch + cx1 * step];
    return (1.0f - wy1) * r0 + wy1 * r1;
}

/*
 * 8-bit YUV 4:2:0 (planar or semi-planar) -> packed RGB48.
 * One thread per output pixel. Pitches are in elements of the
 * respective type. uv_step is 1 for planar, 2 for NV12.
 */
__global__ void yuv8_to_rgb48(const unsigned char *src_y, int pitch_y,
                              const unsigned char *src_u, int pitch_u,
                              const unsigned char *src_v, int pitch_v,
                              int uv_step,
                              unsigned short *dst, int dst_pitch,
                              int width, int height,
                              float y_off, float y_scale, float c_scale,
                              float crv, float cgu, float cgv, float cbu)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height)
        return;

    int cw = (width  + 1) >> 1;
    int ch = (height + 1) >> 1;

    float yf = ((float)src_y[y * pitch_y + x] - y_off) * y_scale;
    float cb = (chroma_fetch(src_u, pitch_u, uv_step, cw, ch, x, y) - 128.0f) * c_scale;
    float cr = (chroma_fetch(src_v, pitch_v, uv_step, cw, ch, x, y) - 128.0f) * c_scale;

    float r = yf + crv * cr;
    float g = yf - cgu * cb - cgv * cr;
    float b = yf + cbu * cb;

    unsigned short *out = dst + y * dst_pitch + 3 * x;
    out[0] = (unsigned short)(clampf(r, 0.0f, 1.0f) * 65535.0f + 0.5f);
    out[1] = (unsigned short)(clampf(g, 0.0f, 1.0f) * 65535.0f + 0.5f);
    out[2] = (unsigned short)(clampf(b, 0.0f, 1.0f) * 65535.0f + 0.5f);
}

/*
 * 8-bit YUV 4:2:0 (planar or semi-planar) -> packed float RGB(A) in [0,1].
 * Same as yuv8_to_rgb48 but without the 16-bit quantization. nch is 3 for
 * rgbf32 and 4 for rgbaf32 (alpha written as 1.0).
 */
__global__ void yuv8_to_rgbf32(const unsigned char *src_y, int pitch_y,
                               const unsigned char *src_u, int pitch_u,
                               const unsigned char *src_v, int pitch_v,
                               int uv_step,
                               float *dst, int dst_pitch, int nch,
                               int width, int height,
                               float y_off, float y_scale, float c_scale,
                               float crv, float cgu, float cgv, float cbu)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= width || y >= height)
        return;

    int cw = (width  + 1) >> 1;
    int ch = (height + 1) >> 1;

    float yf = ((float)src_y[y * pitch_y + x] - y_off) * y_scale;
    float cb = (chroma_fetch(src_u, pitch_u, uv_step, cw, ch, x, y) - 128.0f) * c_scale;
    float cr = (chroma_fetch(src_v, pitch_v, uv_step, cw, ch, x, y) - 128.0f) * c_scale;

    float *out = dst + y * dst_pitch + nch * x;
    out[0] = clampf(yf + crv * cr, 0.0f, 1.0f);
    out[1] = clampf(yf - cgu * cb - cgv * cr, 0.0f, 1.0f);
    out[2] = clampf(yf + cbu * cb, 0.0f, 1.0f);
    if (nch == 4)
        out[3] = 1.0f;
}

/*
 * Packed RGB48 -> 8-bit YUV 4:2:0 (planar or semi-planar).
 * One thread per 2x2 luma block: writes 4 luma samples and one U/V pair
 * computed from the box-averaged RGB of the block.
 */
__global__ void rgb48_to_yuv8(const unsigned short *src, int src_pitch,
                              unsigned char *dst_y, int pitch_y,
                              unsigned char *dst_u, int pitch_u,
                              unsigned char *dst_v, int pitch_v,
                              int uv_step,
                              int width, int height,
                              float ry, float gy, float by,
                              float y_off, float y_scale,
                              float ru, float gu, float bu,
                              float rv, float gv, float bv, float c_scale)
{
    int cx = blockIdx.x * blockDim.x + threadIdx.x;
    int cy = blockIdx.y * blockDim.y + threadIdx.y;
    int cw = (width  + 1) >> 1;
    int ch = (height + 1) >> 1;
    if (cx >= cw || cy >= ch)
        return;

    int x0 = 2 * cx, y0 = 2 * cy;
    int x1 = min(x0 + 1, width - 1);
    int y1 = min(y0 + 1, height - 1);

    float ravg = 0.0f, gavg = 0.0f, bavg = 0.0f;

    for (int i = 0; i < 4; i++) {
        int px = (i & 1) ? x1 : x0;
        int py = (i & 2) ? y1 : y0;
        const unsigned short *in = src + py * src_pitch + 3 * px;
        float r = in[0] * (1.0f / 65535.0f);
        float g = in[1] * (1.0f / 65535.0f);
        float b = in[2] * (1.0f / 65535.0f);

        float yf = ry * r + gy * g + by * b;
        dst_y[py * pitch_y + px] =
            (unsigned char)clampi((int)(yf * y_scale + y_off + 0.5f), 0, 255);

        ravg += 0.25f * r;
        gavg += 0.25f * g;
        bavg += 0.25f * b;
    }

    float u = ru * ravg + gu * gavg + bu * bavg;
    float v = rv * ravg + gv * gavg + bv * bavg;
    dst_u[cy * pitch_u + cx * uv_step] =
        (unsigned char)clampi((int)(u * c_scale + 128.5f), 0, 255);
    dst_v[cy * pitch_v + cx * uv_step] =
        (unsigned char)clampi((int)(v * c_scale + 128.5f), 0, 255);
}

/*
 * Packed float RGB in [0,1] -> 8-bit YUV 4:2:0 (planar or semi-planar).
 * Same as rgb48_to_yuv8 without the 1/65535 normalization; out-of-range
 * float inputs (model over/undershoot) are handled by the final clamps.
 */
__global__ void rgbf32_to_yuv8(const float *src, int src_pitch, int nch,
                               unsigned char *dst_y, int pitch_y,
                               unsigned char *dst_u, int pitch_u,
                               unsigned char *dst_v, int pitch_v,
                               int uv_step,
                               int width, int height,
                               float ry, float gy, float by,
                               float y_off, float y_scale,
                               float ru, float gu, float bu,
                               float rv, float gv, float bv, float c_scale)
{
    int cx = blockIdx.x * blockDim.x + threadIdx.x;
    int cy = blockIdx.y * blockDim.y + threadIdx.y;
    int cw = (width  + 1) >> 1;
    int ch = (height + 1) >> 1;
    if (cx >= cw || cy >= ch)
        return;

    int x0 = 2 * cx, y0 = 2 * cy;
    int x1 = min(x0 + 1, width - 1);
    int y1 = min(y0 + 1, height - 1);

    float ravg = 0.0f, gavg = 0.0f, bavg = 0.0f;

    for (int i = 0; i < 4; i++) {
        int px = (i & 1) ? x1 : x0;
        int py = (i & 2) ? y1 : y0;
        const float *in = src + py * src_pitch + nch * px;
        float r = in[0], g = in[1], b = in[2];

        float yf = ry * r + gy * g + by * b;
        dst_y[py * pitch_y + px] =
            (unsigned char)clampi((int)(yf * y_scale + y_off + 0.5f), 0, 255);

        ravg += 0.25f * r;
        gavg += 0.25f * g;
        bavg += 0.25f * b;
    }

    float u = ru * ravg + gu * gavg + bu * bavg;
    float v = rv * ravg + gv * gavg + bv * bavg;
    dst_u[cy * pitch_u + cx * uv_step] =
        (unsigned char)clampi((int)(u * c_scale + 128.5f), 0, 255);
    dst_v[cy * pitch_v + cx * uv_step] =
        (unsigned char)clampi((int)(v * c_scale + 128.5f), 0, 255);
}

}
