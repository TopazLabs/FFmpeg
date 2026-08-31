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

#ifndef SWSCALE_X86_INPUT_RGB48_H
#define SWSCALE_X86_INPUT_RGB48_H

#include <stdint.h>

#define RGB48_INPUT_DECLS(pattern)                                            \
void ff_ ## pattern ## 48LEToY_avx2(uint8_t *dst, const uint8_t *src,         \
                                    const uint8_t *unused0,                   \
                                    const uint8_t *unused1, int width,        \
                                    uint32_t *rgb2yuv, void *opq);            \
void ff_ ## pattern ## 48LEToUV_avx2(uint8_t *dstU, uint8_t *dstV,            \
                                     const uint8_t *unused0,                  \
                                     const uint8_t *src1,                     \
                                     const uint8_t *src2, int width,          \
                                     uint32_t *rgb2yuv, void *opq);           \
void ff_ ## pattern ## 48LEToUV_half_avx2(uint8_t *dstU, uint8_t *dstV,       \
                                          const uint8_t *unused0,             \
                                          const uint8_t *src1,                \
                                          const uint8_t *src2, int width,     \
                                          uint32_t *rgb2yuv, void *opq);

RGB48_INPUT_DECLS(rgb)
RGB48_INPUT_DECLS(bgr)

#endif /* SWSCALE_X86_INPUT_RGB48_H */
