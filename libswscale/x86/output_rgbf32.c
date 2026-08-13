/*
 * AVX2-optimized packed float RGB (rgbf32le) output writers
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

#include "config.h"

#if HAVE_AVX2

#include <immintrin.h>
#include <stdint.h>

#include "libswscale/swscale_internal.h"
#include "output_rgbf32.h"

#if defined(__GNUC__) && !defined(__INTEL_COMPILER)
#define AVX2_FN __attribute__((target("avx2")))
#else
#define AVX2_FN
#endif

/*
 * Bit-exact vectorization of the yuv2rgbf32le_(full_)1_c writers in
 * libswscale/output.c: identical 30-bit integer math (32-bit wraparound
 * semantics preserved via vpmulld), clamp to [0,65535], then an exact
 * int->float conversion scaled by 1/65535.
 */

typedef struct RGBF32Coeffs {
    __m256i yoff, ycoef, ybias;
    __m256i v2r, v2g, u2g, u2b;
    __m256i c32768, c65535;
    __m256 scale;
} RGBF32Coeffs;

AVX2_FN
static av_always_inline RGBF32Coeffs load_coeffs(const SwsInternal *c)
{
    RGBF32Coeffs k;
    k.yoff   = _mm256_set1_epi32(c->yuv2rgb_y_offset);
    k.ycoef  = _mm256_set1_epi32(c->yuv2rgb_y_coeff);
    k.ybias  = _mm256_set1_epi32((1 << 13) - (1 << 29));
    k.v2r    = _mm256_set1_epi32(c->yuv2rgb_v2r_coeff);
    k.v2g    = _mm256_set1_epi32(c->yuv2rgb_v2g_coeff);
    k.u2g    = _mm256_set1_epi32(c->yuv2rgb_u2g_coeff);
    k.u2b    = _mm256_set1_epi32(c->yuv2rgb_u2b_coeff);
    k.c32768 = _mm256_set1_epi32(1 << 15);
    k.c65535 = _mm256_set1_epi32(65535);
    k.scale  = _mm256_set1_ps(1.0f / 65535.0f);
    return k;
}

AVX2_FN
static av_always_inline __m256 rgbf32_channel(const RGBF32Coeffs *k,
                                              __m256i acc, __m256i Y)
{
    __m256i v = _mm256_add_epi32(acc, Y);
    v = _mm256_add_epi32(_mm256_srai_epi32(v, 14), k->c32768);
    v = _mm256_min_epi32(_mm256_max_epi32(v, _mm256_setzero_si256()), k->c65535);
    return _mm256_mul_ps(_mm256_cvtepi32_ps(v), k->scale);
}

/* interleave 8-wide R/G/B float vectors into 24 packed floats */
AVX2_FN
static av_always_inline void store_rgb24f(float *dest, __m256 r, __m256 g, __m256 b)
{
    const __m256i i0r = _mm256_setr_epi32(0, 0, 0, 1, 1, 1, 2, 2);
    const __m256i i0g = _mm256_setr_epi32(0, 0, 0, 0, 1, 1, 1, 2);
    const __m256i i0b = _mm256_setr_epi32(0, 0, 0, 0, 0, 1, 1, 1);
    const __m256i i1r = _mm256_setr_epi32(3, 3, 3, 4, 4, 4, 5, 5);
    const __m256i i1g = _mm256_setr_epi32(2, 2, 3, 3, 3, 4, 4, 4);
    const __m256i i1b = _mm256_setr_epi32(2, 2, 2, 3, 3, 3, 4, 4);
    const __m256i i2r = _mm256_setr_epi32(5, 5, 6, 6, 6, 7, 7, 7);
    const __m256i i2g = _mm256_setr_epi32(5, 5, 5, 6, 6, 6, 7, 7);
    const __m256i i2b = _mm256_setr_epi32(4, 5, 5, 5, 6, 6, 6, 7);
    __m256 t, o;

    /* out0 = r0 g0 b0 r1 g1 b1 r2 g2 */
    t = _mm256_blend_ps(_mm256_permutevar8x32_ps(r, i0r),
                        _mm256_permutevar8x32_ps(g, i0g), 0x92);
    o = _mm256_blend_ps(t, _mm256_permutevar8x32_ps(b, i0b), 0x24);
    _mm256_storeu_ps(dest, o);
    /* out1 = b2 r3 g3 b3 r4 g4 b4 r5 */
    t = _mm256_blend_ps(_mm256_permutevar8x32_ps(r, i1r),
                        _mm256_permutevar8x32_ps(g, i1g), 0x24);
    o = _mm256_blend_ps(t, _mm256_permutevar8x32_ps(b, i1b), 0x49);
    _mm256_storeu_ps(dest + 8, o);
    /* out2 = g5 b5 r6 g6 b6 r7 g7 b7 */
    t = _mm256_blend_ps(_mm256_permutevar8x32_ps(r, i2r),
                        _mm256_permutevar8x32_ps(g, i2g), 0x49);
    o = _mm256_blend_ps(t, _mm256_permutevar8x32_ps(b, i2b), 0x92);
    _mm256_storeu_ps(dest + 16, o);
}

AVX2_FN
static av_always_inline void rgbf32_kernel(const RGBF32Coeffs *k,
                                           __m256i Y, __m256i U, __m256i V,
                                           float *dest)
{
    __m256i R, G, B;

    Y = _mm256_sub_epi32(Y, k->yoff);
    Y = _mm256_mullo_epi32(Y, k->ycoef);
    Y = _mm256_add_epi32(Y, k->ybias);

    R = _mm256_mullo_epi32(V, k->v2r);
    G = _mm256_add_epi32(_mm256_mullo_epi32(V, k->v2g),
                         _mm256_mullo_epi32(U, k->u2g));
    B = _mm256_mullo_epi32(U, k->u2b);

    store_rgb24f(dest,
                 rgbf32_channel(k, R, Y),
                 rgbf32_channel(k, G, Y),
                 rgbf32_channel(k, B, Y));
}

/* scalar tail, identical to the C writers */
static av_always_inline void rgbf32_tail(const SwsInternal *c, int Y, int U, int V,
                                         float *dest)
{
    SUINT Ys = (SUINT)Y;
    int R, G, B;

    Ys -= c->yuv2rgb_y_offset;
    Ys *= c->yuv2rgb_y_coeff;
    Ys += (1 << 13) - (1 << 29);

    R = V * c->yuv2rgb_v2r_coeff;
    G = V * c->yuv2rgb_v2g_coeff + U * c->yuv2rgb_u2g_coeff;
    B =                            U * c->yuv2rgb_u2b_coeff;

    dest[0] = (1.0f / 65535.0f) * (float)av_clip_uintp2(((int)(R + Ys) >> 14) + (1 << 15), 16);
    dest[1] = (1.0f / 65535.0f) * (float)av_clip_uintp2(((int)(G + Ys) >> 14) + (1 << 15), 16);
    dest[2] = (1.0f / 65535.0f) * (float)av_clip_uintp2(((int)(B + Ys) >> 14) + (1 << 15), 16);
}

AVX2_FN
void ff_yuv2rgbf32le_full_1_avx2(SwsInternal *c, const int16_t *_buf0,
                                 const int16_t *_ubuf[2], const int16_t *_vbuf[2],
                                 const int16_t *_abuf0, uint8_t *_dest, int dstW,
                                 int uvalpha, int y)
{
    const int32_t  *buf0 = (const int32_t *)  _buf0;
    const int32_t **ubuf = (const int32_t **) _ubuf;
    const int32_t **vbuf = (const int32_t **) _vbuf;
    const int32_t *ubuf0 = ubuf[0], *vbuf0 = vbuf[0];
    float *dest = (float *) _dest;
    const RGBF32Coeffs k = load_coeffs(c);
    int i = 0;

    if (uvalpha == 0) {
        const __m256i coff = _mm256_set1_epi32(128 << 11);
        for (; i + 8 <= dstW; i += 8) {
            __m256i Y = _mm256_srai_epi32(_mm256_loadu_si256((const __m256i *)(buf0 + i)), 2);
            __m256i U = _mm256_srai_epi32(_mm256_sub_epi32(
                            _mm256_loadu_si256((const __m256i *)(ubuf0 + i)), coff), 2);
            __m256i V = _mm256_srai_epi32(_mm256_sub_epi32(
                            _mm256_loadu_si256((const __m256i *)(vbuf0 + i)), coff), 2);
            rgbf32_kernel(&k, Y, U, V, dest + 3 * i);
        }
        for (; i < dstW; i++)
            rgbf32_tail(c, buf0[i] >> 2,
                        (ubuf0[i] - (128 << 11)) >> 2,
                        (vbuf0[i] - (128 << 11)) >> 2,
                        dest + 3 * i);
    } else {
        const int32_t *ubuf1 = ubuf[1], *vbuf1 = vbuf[1];
        unsigned uvalpha1 = 4096 - uvalpha;
        const __m256i ua  = _mm256_set1_epi32(uvalpha);
        const __m256i ua1 = _mm256_set1_epi32(uvalpha1);
        const __m256i coff = _mm256_set1_epi32(128 << 23);

        for (; i + 8 <= dstW; i += 8) {
            __m256i Y = _mm256_srai_epi32(_mm256_loadu_si256((const __m256i *)(buf0 + i)), 2);
            __m256i U = _mm256_srli_epi32(_mm256_sub_epi32(_mm256_add_epi32(
                            _mm256_mullo_epi32(_mm256_loadu_si256((const __m256i *)(ubuf0 + i)), ua1),
                            _mm256_mullo_epi32(_mm256_loadu_si256((const __m256i *)(ubuf1 + i)), ua)),
                            coff), 14);
            __m256i V = _mm256_srli_epi32(_mm256_sub_epi32(_mm256_add_epi32(
                            _mm256_mullo_epi32(_mm256_loadu_si256((const __m256i *)(vbuf0 + i)), ua1),
                            _mm256_mullo_epi32(_mm256_loadu_si256((const __m256i *)(vbuf1 + i)), ua)),
                            coff), 14);
            rgbf32_kernel(&k, Y, U, V, dest + 3 * i);
        }
        for (; i < dstW; i++)
            rgbf32_tail(c, buf0[i] >> 2,
                        (int)(((SUINT)ubuf0[i] * uvalpha1 + (SUINT)ubuf1[i] * uvalpha - (128 << 23)) >> 14),
                        (int)(((SUINT)vbuf0[i] * uvalpha1 + (SUINT)vbuf1[i] * uvalpha - (128 << 23)) >> 14),
                        dest + 3 * i);
    }
}

AVX2_FN
void ff_yuv2rgbf32le_1_avx2(SwsInternal *c, const int16_t *_buf0,
                            const int16_t *_ubuf[2], const int16_t *_vbuf[2],
                            const int16_t *_abuf0, uint8_t *_dest, int dstW,
                            int uvalpha, int y)
{
    const int32_t  *buf0 = (const int32_t *)  _buf0;
    const int32_t **ubuf = (const int32_t **) _ubuf;
    const int32_t **vbuf = (const int32_t **) _vbuf;
    const int32_t *ubuf0 = ubuf[0], *vbuf0 = vbuf[0];
    float *dest = (float *) _dest;
    const RGBF32Coeffs k = load_coeffs(c);
    /* duplicate each of 4 chroma samples over a pixel pair */
    const __m256i dup = _mm256_setr_epi32(0, 0, 1, 1, 2, 2, 3, 3);
    int i = 0;

    if (uvalpha == 0) {
        const __m128i coff = _mm_set1_epi32(128 << 11);
        for (; i + 8 <= dstW; i += 8) {
            __m256i Y = _mm256_srai_epi32(_mm256_loadu_si256((const __m256i *)(buf0 + i)), 2);
            __m128i u4 = _mm_srai_epi32(_mm_sub_epi32(
                             _mm_loadu_si128((const __m128i *)(ubuf0 + i / 2)), coff), 2);
            __m128i v4 = _mm_srai_epi32(_mm_sub_epi32(
                             _mm_loadu_si128((const __m128i *)(vbuf0 + i / 2)), coff), 2);
            __m256i U = _mm256_permutevar8x32_epi32(_mm256_castsi128_si256(u4), dup);
            __m256i V = _mm256_permutevar8x32_epi32(_mm256_castsi128_si256(v4), dup);
            rgbf32_kernel(&k, Y, U, V, dest + 3 * i);
        }
        for (; i < dstW; i += 2) {
            int U = (ubuf0[i / 2] - (128 << 11)) >> 2;
            int V = (vbuf0[i / 2] - (128 << 11)) >> 2;
            rgbf32_tail(c, buf0[i] >> 2, U, V, dest + 3 * i);
            if (i + 1 < dstW)
                rgbf32_tail(c, buf0[i + 1] >> 2, U, V, dest + 3 * (i + 1));
        }
    } else {
        const int32_t *ubuf1 = ubuf[1], *vbuf1 = vbuf[1];
        unsigned uvalpha1 = 4096 - uvalpha;
        const __m128i ua  = _mm_set1_epi32(uvalpha);
        const __m128i ua1 = _mm_set1_epi32(uvalpha1);
        const __m128i coff = _mm_set1_epi32(128 << 23);

        for (; i + 8 <= dstW; i += 8) {
            __m256i Y = _mm256_srai_epi32(_mm256_loadu_si256((const __m256i *)(buf0 + i)), 2);
            __m128i u4 = _mm_srli_epi32(_mm_sub_epi32(_mm_add_epi32(
                             _mm_mullo_epi32(_mm_loadu_si128((const __m128i *)(ubuf0 + i / 2)), ua1),
                             _mm_mullo_epi32(_mm_loadu_si128((const __m128i *)(ubuf1 + i / 2)), ua)),
                             coff), 14);
            __m128i v4 = _mm_srli_epi32(_mm_sub_epi32(_mm_add_epi32(
                             _mm_mullo_epi32(_mm_loadu_si128((const __m128i *)(vbuf0 + i / 2)), ua1),
                             _mm_mullo_epi32(_mm_loadu_si128((const __m128i *)(vbuf1 + i / 2)), ua)),
                             coff), 14);
            __m256i U = _mm256_permutevar8x32_epi32(_mm256_castsi128_si256(u4), dup);
            __m256i V = _mm256_permutevar8x32_epi32(_mm256_castsi128_si256(v4), dup);
            rgbf32_kernel(&k, Y, U, V, dest + 3 * i);
        }
        for (; i < dstW; i += 2) {
            int U = (int)(((SUINT)ubuf0[i / 2] * uvalpha1 + (SUINT)ubuf1[i / 2] * uvalpha - (128 << 23)) >> 14);
            int V = (int)(((SUINT)vbuf0[i / 2] * uvalpha1 + (SUINT)vbuf1[i / 2] * uvalpha - (128 << 23)) >> 14);
            rgbf32_tail(c, buf0[i] >> 2, U, V, dest + 3 * i);
            if (i + 1 < dstW)
                rgbf32_tail(c, buf0[i + 1] >> 2, U, V, dest + 3 * (i + 1));
        }
    }
}

#endif /* HAVE_AVX2 */
