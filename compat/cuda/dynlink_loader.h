/*
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

#ifndef COMPAT_CUDA_DYNLINK_LOADER_H
#define COMPAT_CUDA_DYNLINK_LOADER_H

#include "libavutil/log.h"
#include "compat/w32dlfcn.h"

#if defined(_WIN32) && (defined(_M_ARM64) || defined(__aarch64__))
#include <string.h>

/*
 * On Windows on Arm the NVIDIA driver ships the native ARM64 NVENC/NVDEC
 * libraries as nvEncodeAPIa64.dll and nvcuvida64.dll, while nvEncodeAPI64.dll
 * and nvcuvid.dll are the x64 builds used under emulation. Prefer the native
 * ones and fall back to the name requested by ffnvcodec.
 */
static inline void *ffnv_win_arm64_dlopen(const char *path)
{
    const char *native = NULL;
    void *lib;

    if (!strcmp(path, "nvEncodeAPI64.dll") || !strcmp(path, "nvEncodeAPI.dll"))
        native = "nvEncodeAPIa64.dll";
    else if (!strcmp(path, "nvcuvid.dll"))
        native = "nvcuvida64.dll";

    if (native && (lib = dlopen(native, RTLD_LAZY)))
        return lib;
    return dlopen(path, RTLD_LAZY);
}

#define FFNV_LOAD_FUNC(path) ffnv_win_arm64_dlopen(path)
#else
#define FFNV_LOAD_FUNC(path) dlopen((path), RTLD_LAZY)
#endif
#define FFNV_SYM_FUNC(lib, sym) dlsym((lib), (sym))
#define FFNV_FREE_FUNC(lib) dlclose(lib)
#define FFNV_LOG_FUNC(logctx, msg, ...) av_log(logctx, AV_LOG_ERROR, msg,  __VA_ARGS__)
#define FFNV_DEBUG_LOG_FUNC(logctx, msg, ...) av_log(logctx, AV_LOG_DEBUG, msg,  __VA_ARGS__)

#include <ffnvcodec/dynlink_loader.h>

#endif /* COMPAT_CUDA_DYNLINK_LOADER_H */
