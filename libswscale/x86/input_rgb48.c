/*
 * AVX2-optimized RGB48/BGR48 -> YUV input converters
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
#include "input_rgb48.h"

#if defined(__GNUC__) && !defined(__INTEL_COMPILER)
#define AVX2_FN __attribute__((target("avx2")))
#else
#define AVX2_FN
#endif

/*
 * The 16-bit unsigned samples are XORed with 0x8000 so pmaddwd can treat
 * them as signed: c*u == c*(u - 32768) + 32768*c. The constant part,
 * 32768*(cr+cg+cb), is folded into the rounding bias. All arithmetic wraps
 * mod 2^32 exactly like the C reference, so results are bit-identical.
 */

/* Gather [r0 g0 r1 g1 ...] words per 128-bit lane from two overlapping
 * loads of one 24-byte half-block (lane offsets: v0 = bytes 0-15,
 * v1 = bytes 8-23 of the half-block). */
#define RG_M0 _mm256_setr_epi8(  0,  1,  2,  3,  6,  7,  8,  9, 12, 13, 14, 15, -1, -1, -1, -1, \
                                 0,  1,  2,  3,  6,  7,  8,  9, 12, 13, 14, 15, -1, -1, -1, -1)
#define RG_M1 _mm256_setr_epi8( -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 10, 11, 12, 13, \
                                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 10, 11, 12, 13)
/* Gather [b0 0 b1 0 ...] words (odd word lanes are zero, multiplied by 0). */
#define B_M0  _mm256_setr_epi8(  4,  5, -1, -1, 10, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, \
                                 4,  5, -1, -1, 10, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1)
#define B_M1  _mm256_setr_epi8( -1, -1, -1, -1, -1, -1, -1, -1,  8,  9, -1, -1, 14, 15, -1, -1, \
                                -1, -1, -1, -1, -1, -1, -1, -1,  8,  9, -1, -1, 14, 15, -1, -1)
/* Truncate dwords to their low words (like the C uint16_t store). */
#define PACK_M _mm256_setr_epi8( 0,  1,  4,  5,  8,  9, 12, 13, -1, -1, -1, -1, -1, -1, -1, -1, \
                                 0,  1,  4,  5,  8,  9, 12, 13, -1, -1, -1, -1, -1, -1, -1, -1)

/* Even/odd pixel gathers for the _half variant: one 128-bit lane covers a
 * 48-byte half-block (8 input pixels) via three loads:
 * v0 = bytes 0-15, v1 = bytes 16-31, v2 = bytes 32-47. */
#define RG_E_M0 _mm256_setr_epi8(  0,  1,  2,  3, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, \
                                   0,  1,  2,  3, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1)
#define RG_E_M1 _mm256_setr_epi8( -1, -1, -1, -1, -1, -1, -1, -1,  8,  9, 10, 11, -1, -1, -1, -1, \
                                  -1, -1, -1, -1, -1, -1, -1, -1,  8,  9, 10, 11, -1, -1, -1, -1)
#define RG_E_M2 _mm256_setr_epi8( -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  4,  5,  6,  7, \
                                  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  4,  5,  6,  7)
#define RG_O_M0 _mm256_setr_epi8(  6,  7,  8,  9, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, \
                                   6,  7,  8,  9, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1)
#define RG_O_M1 _mm256_setr_epi8( -1, -1, -1, -1,  2,  3,  4,  5, 14, 15, -1, -1, -1, -1, -1, -1, \
                                  -1, -1, -1, -1,  2,  3,  4,  5, 14, 15, -1, -1, -1, -1, -1, -1)
#define RG_O_M2 _mm256_setr_epi8( -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  0,  1, 10, 11, 12, 13, \
                                  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  0,  1, 10, 11, 12, 13)
#define B_E_M0  _mm256_setr_epi8(  4,  5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, \
                                   4,  5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1)
#define B_E_M1  _mm256_setr_epi8( -1, -1, -1, -1,  0,  1, -1, -1, 12, 13, -1, -1, -1, -1, -1, -1, \
                                  -1, -1, -1, -1,  0,  1, -1, -1, 12, 13, -1, -1, -1, -1, -1, -1)
#define B_E_M2  _mm256_setr_epi8( -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  8,  9, -1, -1, \
                                  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  8,  9, -1, -1)
#define B_O_M0  _mm256_setr_epi8( 10, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, \
                                  10, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1)
#define B_O_M1  _mm256_setr_epi8( -1, -1, -1, -1,  6,  7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, \
                                  -1, -1, -1, -1,  6,  7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1)
#define B_O_M2  _mm256_setr_epi8( -1, -1, -1, -1, -1, -1, -1, -1,  2,  3, -1, -1, 14, 15, -1, -1, \
                                  -1, -1, -1, -1, -1, -1, -1, -1,  2,  3, -1, -1, 14, 15, -1, -1)

#define COEFF_RG(c1, c2) \
    _mm256_set1_epi32((uint32_t)(uint16_t)(c1) | ((uint32_t)(uint16_t)(c2) << 16))
#define COEFF_B(c) _mm256_set1_epi32((uint32_t)(uint16_t)(c))

AVX2_FN
static __m256i load_2x24(const uint8_t *p, __m256i *v1)
{
    __m256i v0 = _mm256_inserti128_si256(
        _mm256_castsi128_si256(_mm_loadu_si128((const __m128i *)(p +  0))),
        _mm_loadu_si128((const __m128i *)(p + 24)), 1);
    *v1 = _mm256_inserti128_si256(
        _mm256_castsi128_si256(_mm_loadu_si128((const __m128i *)(p +  8))),
        _mm_loadu_si128((const __m128i *)(p + 32)), 1);
    return v0;
}

AVX2_FN
static __m128i cvt32to16(__m256i sum)
{
    __m256i w = _mm256_shuffle_epi8(sum, PACK_M);
    return _mm256_castsi256_si128(_mm256_permute4x64_epi64(w, 0x08));
}

AVX2_FN
static av_always_inline void rgb48_to_y_avx2(uint8_t *_dst, const uint8_t *src,
                                             int width, int32_t cr, int32_t cg,
                                             int32_t cb)
{
    uint16_t *dst = (uint16_t *)_dst;
    const __m256i c_rg  = COEFF_RG(cr, cg);
    const __m256i c_b   = COEFF_B(cb);
    const __m256i sign  = _mm256_set1_epi16(-0x8000);
    const __m256i biasv = _mm256_set1_epi32(
        ((uint32_t)(cr + cg + cb) << 15) + (0x2001u << (RGB2YUV_SHIFT - 1)));
    int i = 0;

    for (; i + 8 <= width; i += 8) {
        const uint8_t *p = src + 6 * i;
        __m256i v1, v0 = load_2x24(p, &v1);
        __m256i rg = _mm256_or_si256(_mm256_shuffle_epi8(v0, RG_M0),
                                     _mm256_shuffle_epi8(v1, RG_M1));
        __m256i b  = _mm256_or_si256(_mm256_shuffle_epi8(v0, B_M0),
                                     _mm256_shuffle_epi8(v1, B_M1));
        __m256i sum;
        rg  = _mm256_xor_si256(rg, sign);
        b   = _mm256_xor_si256(b,  sign);
        sum = _mm256_add_epi32(_mm256_madd_epi16(rg, c_rg),
                               _mm256_madd_epi16(b,  c_b));
        sum = _mm256_srai_epi32(_mm256_add_epi32(sum, biasv), RGB2YUV_SHIFT);
        _mm_storeu_si128((__m128i *)(dst + i), cvt32to16(sum));
    }
    for (; i < width; i++) {
        const uint16_t *s = (const uint16_t *)(src + 6 * i);
        unsigned int c1 = s[0], c2 = s[1], c3 = s[2];
        dst[i] = (cr * c1 + cg * c2 + cb * c3 +
                  (0x2001 << (RGB2YUV_SHIFT - 1))) >> RGB2YUV_SHIFT;
    }
}

AVX2_FN
static av_always_inline void rgb48_to_uv_avx2(uint8_t *_dstU, uint8_t *_dstV,
                                              const uint8_t *src, int width,
                                              const int32_t cu[3],
                                              const int32_t cv[3])
{
    uint16_t *dstU = (uint16_t *)_dstU;
    uint16_t *dstV = (uint16_t *)_dstV;
    const __m256i cu_rg = COEFF_RG(cu[0], cu[1]);
    const __m256i cu_b  = COEFF_B(cu[2]);
    const __m256i cv_rg = COEFF_RG(cv[0], cv[1]);
    const __m256i cv_b  = COEFF_B(cv[2]);
    const __m256i sign  = _mm256_set1_epi16(-0x8000);
    const __m256i biasu = _mm256_set1_epi32(
        ((uint32_t)(cu[0] + cu[1] + cu[2]) << 15) + (0x10001u << (RGB2YUV_SHIFT - 1)));
    const __m256i biasw = _mm256_set1_epi32(
        ((uint32_t)(cv[0] + cv[1] + cv[2]) << 15) + (0x10001u << (RGB2YUV_SHIFT - 1)));
    int i = 0;

    for (; i + 8 <= width; i += 8) {
        const uint8_t *p = src + 6 * i;
        __m256i v1, v0 = load_2x24(p, &v1);
        __m256i rg = _mm256_or_si256(_mm256_shuffle_epi8(v0, RG_M0),
                                     _mm256_shuffle_epi8(v1, RG_M1));
        __m256i b  = _mm256_or_si256(_mm256_shuffle_epi8(v0, B_M0),
                                     _mm256_shuffle_epi8(v1, B_M1));
        __m256i su, sv;
        rg = _mm256_xor_si256(rg, sign);
        b  = _mm256_xor_si256(b,  sign);
        su = _mm256_add_epi32(_mm256_madd_epi16(rg, cu_rg),
                              _mm256_madd_epi16(b,  cu_b));
        sv = _mm256_add_epi32(_mm256_madd_epi16(rg, cv_rg),
                              _mm256_madd_epi16(b,  cv_b));
        su = _mm256_srai_epi32(_mm256_add_epi32(su, biasu), RGB2YUV_SHIFT);
        sv = _mm256_srai_epi32(_mm256_add_epi32(sv, biasw), RGB2YUV_SHIFT);
        _mm_storeu_si128((__m128i *)(dstU + i), cvt32to16(su));
        _mm_storeu_si128((__m128i *)(dstV + i), cvt32to16(sv));
    }
    for (; i < width; i++) {
        const uint16_t *s = (const uint16_t *)(src + 6 * i);
        unsigned int c1 = s[0], c2 = s[1], c3 = s[2];
        dstU[i] = (cu[0] * c1 + cu[1] * c2 + cu[2] * c3 +
                   (0x10001 << (RGB2YUV_SHIFT - 1))) >> RGB2YUV_SHIFT;
        dstV[i] = (cv[0] * c1 + cv[1] * c2 + cv[2] * c3 +
                   (0x10001 << (RGB2YUV_SHIFT - 1))) >> RGB2YUV_SHIFT;
    }
}

AVX2_FN
static av_always_inline void rgb48_to_uv_half_avx2(uint8_t *_dstU, uint8_t *_dstV,
                                                   const uint8_t *src, int width,
                                                   const int32_t cu[3],
                                                   const int32_t cv[3])
{
    uint16_t *dstU = (uint16_t *)_dstU;
    uint16_t *dstV = (uint16_t *)_dstV;
    const __m256i cu_rg = COEFF_RG(cu[0], cu[1]);
    const __m256i cu_b  = COEFF_B(cu[2]);
    const __m256i cv_rg = COEFF_RG(cv[0], cv[1]);
    const __m256i cv_b  = COEFF_B(cv[2]);
    const __m256i sign  = _mm256_set1_epi16(-0x8000);
    const __m256i biasu = _mm256_set1_epi32(
        ((uint32_t)(cu[0] + cu[1] + cu[2]) << 15) + (0x10001u << (RGB2YUV_SHIFT - 1)));
    const __m256i biasw = _mm256_set1_epi32(
        ((uint32_t)(cv[0] + cv[1] + cv[2]) << 15) + (0x10001u << (RGB2YUV_SHIFT - 1)));
    int i = 0;

    for (; i + 8 <= width; i += 8) {
        const uint8_t *p = src + 12 * i;
        __m256i v0 = _mm256_inserti128_si256(
            _mm256_castsi128_si256(_mm_loadu_si128((const __m128i *)(p +  0))),
            _mm_loadu_si128((const __m128i *)(p + 48)), 1);
        __m256i v1 = _mm256_inserti128_si256(
            _mm256_castsi128_si256(_mm_loadu_si128((const __m128i *)(p + 16))),
            _mm_loadu_si128((const __m128i *)(p + 64)), 1);
        __m256i v2 = _mm256_inserti128_si256(
            _mm256_castsi128_si256(_mm_loadu_si128((const __m128i *)(p + 32))),
            _mm_loadu_si128((const __m128i *)(p + 80)), 1);
        __m256i rg_e = _mm256_or_si256(_mm256_shuffle_epi8(v0, RG_E_M0),
                       _mm256_or_si256(_mm256_shuffle_epi8(v1, RG_E_M1),
                                       _mm256_shuffle_epi8(v2, RG_E_M2)));
        __m256i rg_o = _mm256_or_si256(_mm256_shuffle_epi8(v0, RG_O_M0),
                       _mm256_or_si256(_mm256_shuffle_epi8(v1, RG_O_M1),
                                       _mm256_shuffle_epi8(v2, RG_O_M2)));
        __m256i b_e  = _mm256_or_si256(_mm256_shuffle_epi8(v0, B_E_M0),
                       _mm256_or_si256(_mm256_shuffle_epi8(v1, B_E_M1),
                                       _mm256_shuffle_epi8(v2, B_E_M2)));
        __m256i b_o  = _mm256_or_si256(_mm256_shuffle_epi8(v0, B_O_M0),
                       _mm256_or_si256(_mm256_shuffle_epi8(v1, B_O_M1),
                                       _mm256_shuffle_epi8(v2, B_O_M2)));
        /* unsigned (a + b + 1) >> 1, exactly like the C reference */
        __m256i rg = _mm256_avg_epu16(rg_e, rg_o);
        __m256i b  = _mm256_avg_epu16(b_e, b_o);
        __m256i su, sv;
        rg = _mm256_xor_si256(rg, sign);
        b  = _mm256_xor_si256(b,  sign);
        su = _mm256_add_epi32(_mm256_madd_epi16(rg, cu_rg),
                              _mm256_madd_epi16(b,  cu_b));
        sv = _mm256_add_epi32(_mm256_madd_epi16(rg, cv_rg),
                              _mm256_madd_epi16(b,  cv_b));
        su = _mm256_srai_epi32(_mm256_add_epi32(su, biasu), RGB2YUV_SHIFT);
        sv = _mm256_srai_epi32(_mm256_add_epi32(sv, biasw), RGB2YUV_SHIFT);
        _mm_storeu_si128((__m128i *)(dstU + i), cvt32to16(su));
        _mm_storeu_si128((__m128i *)(dstV + i), cvt32to16(sv));
    }
    for (; i < width; i++) {
        const uint16_t *s = (const uint16_t *)(src + 12 * i);
        unsigned int c1 = (s[0] + s[3] + 1) >> 1;
        unsigned int c2 = (s[1] + s[4] + 1) >> 1;
        unsigned int c3 = (s[2] + s[5] + 1) >> 1;
        dstU[i] = (cu[0] * c1 + cu[1] * c2 + cu[2] * c3 +
                   (0x10001 << (RGB2YUV_SHIFT - 1))) >> RGB2YUV_SHIFT;
        dstV[i] = (cv[0] * c1 + cv[1] * c2 + cv[2] * c3 +
                   (0x10001 << (RGB2YUV_SHIFT - 1))) >> RGB2YUV_SHIFT;
    }
}

/* r_shift = 0 for RGB48, 2 for BGR48 (component order in memory) */
#define RGB48_FUNCS(pattern, r, b)                                            \
AVX2_FN                                                                       \
void ff_ ## pattern ## 48LEToY_avx2(uint8_t *dst, const uint8_t *src,         \
                                    const uint8_t *unused0,                   \
                                    const uint8_t *unused1, int width,        \
                                    uint32_t *rgb2yuv, void *opq)             \
{                                                                             \
    const int32_t *tab = (const int32_t *)rgb2yuv;                            \
    int32_t c[3];                                                             \
    c[r] = tab[RY_IDX]; c[1] = tab[GY_IDX]; c[b] = tab[BY_IDX];               \
    rgb48_to_y_avx2(dst, src, width, c[0], c[1], c[2]);                       \
}                                                                             \
                                                                              \
AVX2_FN                                                                       \
void ff_ ## pattern ## 48LEToUV_avx2(uint8_t *dstU, uint8_t *dstV,            \
                                     const uint8_t *unused0,                  \
                                     const uint8_t *src1,                     \
                                     const uint8_t *src2, int width,          \
                                     uint32_t *rgb2yuv, void *opq)            \
{                                                                             \
    const int32_t *tab = (const int32_t *)rgb2yuv;                            \
    int32_t cu[3], cv[3];                                                     \
    cu[r] = tab[RU_IDX]; cu[1] = tab[GU_IDX]; cu[b] = tab[BU_IDX];            \
    cv[r] = tab[RV_IDX]; cv[1] = tab[GV_IDX]; cv[b] = tab[BV_IDX];            \
    rgb48_to_uv_avx2(dstU, dstV, src1, width, cu, cv);                        \
}                                                                             \
                                                                              \
AVX2_FN                                                                       \
void ff_ ## pattern ## 48LEToUV_half_avx2(uint8_t *dstU, uint8_t *dstV,       \
                                          const uint8_t *unused0,             \
                                          const uint8_t *src1,                \
                                          const uint8_t *src2, int width,     \
                                          uint32_t *rgb2yuv, void *opq)       \
{                                                                             \
    const int32_t *tab = (const int32_t *)rgb2yuv;                            \
    int32_t cu[3], cv[3];                                                     \
    cu[r] = tab[RU_IDX]; cu[1] = tab[GU_IDX]; cu[b] = tab[BU_IDX];            \
    cv[r] = tab[RV_IDX]; cv[1] = tab[GV_IDX]; cv[b] = tab[BV_IDX];            \
    rgb48_to_uv_half_avx2(dstU, dstV, src1, width, cu, cv);                   \
}

RGB48_FUNCS(rgb, 0, 2)
RGB48_FUNCS(bgr, 2, 0)

#endif /* HAVE_AVX2 */
