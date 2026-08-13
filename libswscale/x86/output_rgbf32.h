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

#ifndef SWSCALE_X86_OUTPUT_RGBF32_H
#define SWSCALE_X86_OUTPUT_RGBF32_H

#include <stdint.h>

#include "libswscale/swscale_internal.h"

void ff_yuv2rgbf32le_full_1_avx2(SwsInternal *c, const int16_t *buf0,
                                 const int16_t *ubuf[2], const int16_t *vbuf[2],
                                 const int16_t *abuf0, uint8_t *dest, int dstW,
                                 int uvalpha, int y);
void ff_yuv2rgbf32le_1_avx2(SwsInternal *c, const int16_t *buf0,
                            const int16_t *ubuf[2], const int16_t *vbuf[2],
                            const int16_t *abuf0, uint8_t *dest, int dstW,
                            int uvalpha, int y);

#endif /* SWSCALE_X86_OUTPUT_RGBF32_H */
