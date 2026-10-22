/*****************************************************************************
 * android_nativewindow.c
 *****************************************************************************
 *
 * Copyright (c) 2013 Bilibili
 * copyright (c) 2013 Zhang Rui <bbcallen@gmail.com>
 *
 * This file is part of ijkPlayer.
 *
 * ijkPlayer is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * ijkPlayer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with ijkPlayer; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "android_nativewindow.h"

#include <assert.h>
#include <stdint.h>
#include <android/native_window.h>
#include "../ijksdl_vout.h"
#include "../ijksdl_vout_internal.h"
#include "../ffmpeg/ijksdl_inc_ffmpeg.h"
#include "ijksdl_inc_internal_android.h"

#ifdef SDLTRACE
#undef SDLTRACE
#define SDLTRACE(...)
#endif

/*
 * Software decode normally asks the surface to take the video's own size, so
 * a 360p stream on an 800x480 screen stays in the top-left corner.  The app
 * asks for an aspect-fit surface; when that surface is larger than the decoded
 * frame, scale the frame into it instead of shrinking the surface back.
 *
 * Nearest-neighbour is intentional: it is cheap enough for 360p on the old
 * ARM targets and only runs on the upscale path.
 */
static void android_scale_plane_nearest(uint8_t *dst, int dst_stride,
                                        int dst_w, int dst_h,
                                        const uint8_t *src, int src_stride,
                                        int src_w, int src_h)
{
    if (!dst || !src || dst_w <= 0 || dst_h <= 0 || src_w <= 0 || src_h <= 0)
        return;

    int step_x = (int)(((int64_t)src_w << 16) / dst_w);
    int step_y = (int)(((int64_t)src_h << 16) / dst_h);
    int sy = 0;

    for (int y = 0; y < dst_h; ++y) {
        int syi = sy >> 16;
        if (syi >= src_h) syi = src_h - 1;
        const uint8_t *src_row = src + (size_t)syi * src_stride;
        uint8_t *dst_row = dst + (size_t)y * dst_stride;
        int sx = 0;
        for (int x = 0; x < dst_w; ++x) {
            int sxi = sx >> 16;
            if (sxi >= src_w) sxi = src_w - 1;
            dst_row[x] = src_row[sxi];
            sx += step_x;
        }
        sy += step_y;
    }
}

static int android_render_yv12_on_yv12_scaled(ANativeWindow_Buffer *out_buffer, const SDL_VoutOverlay *overlay)
{
    int dst_w = out_buffer->width;
    int dst_h = out_buffer->height;
    int src_w = overlay->w;
    int src_h = overlay->h;

    int dst_y_stride = out_buffer->stride;
    int dst_c_stride = IJKALIGN(out_buffer->stride / 2, 16);
    int dst_y_size = dst_y_stride * dst_h;
    int dst_c_size = dst_c_stride * (dst_h / 2);

    uint8_t *dst[3] = {
        out_buffer->bits,
        out_buffer->bits + dst_y_size,
        out_buffer->bits + dst_y_size + dst_c_size,
    };
    int dst_strides[3] = { dst_y_stride, dst_c_stride, dst_c_stride };
    int dst_w_array[3] = { dst_w, dst_w / 2, dst_w / 2 };
    int dst_h_array[3] = { dst_h, dst_h / 2, dst_h / 2 };
    int src_w_array[3] = { src_w, src_w / 2, src_w / 2 };
    int src_h_array[3] = { src_h, src_h / 2, src_h / 2 };

    for (int i = 0; i < 3; ++i) {
        android_scale_plane_nearest(dst[i], dst_strides[i], dst_w_array[i], dst_h_array[i],
                                    overlay->pixels[i], overlay->pitches[i],
                                    src_w_array[i], src_h_array[i]);
    }
    return 0;
}

static int android_render_rgb_on_rgb_scaled(ANativeWindow_Buffer *out_buffer,
                                            const SDL_VoutOverlay *overlay, int bpp)
{
    int bytes = bpp / 8;
    int dst_w = out_buffer->width;
    int dst_h = out_buffer->height;
    int src_w = overlay->w;
    int src_h = overlay->h;
    int dst_stride = out_buffer->stride * bytes;
    int src_stride = overlay->pitches[0];
    uint8_t *dst_base = out_buffer->bits;
    const uint8_t *src_base = overlay->pixels[0];

    int step_x = (int)(((int64_t)src_w << 16) / dst_w);
    int step_y = (int)(((int64_t)src_h << 16) / dst_h);
    int sy = 0;

    for (int y = 0; y < dst_h; ++y) {
        int syi = sy >> 16;
        if (syi >= src_h) syi = src_h - 1;
        uint8_t *dst_row = dst_base + (size_t)y * dst_stride;
        const uint8_t *src_row = src_base + (size_t)syi * src_stride;
        int sx = 0;
        if (bytes == 4) {
            uint32_t *dst_pixels = (uint32_t *)dst_row;
            const uint32_t *src_pixels = (const uint32_t *)src_row;
            for (int x = 0; x < dst_w; ++x) {
                int sxi = sx >> 16;
                if (sxi >= src_w) sxi = src_w - 1;
                dst_pixels[x] = src_pixels[sxi];
                sx += step_x;
            }
        } else if (bytes == 2) {
            uint16_t *dst_pixels = (uint16_t *)dst_row;
            const uint16_t *src_pixels = (const uint16_t *)src_row;
            for (int x = 0; x < dst_w; ++x) {
                int sxi = sx >> 16;
                if (sxi >= src_w) sxi = src_w - 1;
                dst_pixels[x] = src_pixels[sxi];
                sx += step_x;
            }
        } else {
            for (int x = 0; x < dst_w; ++x) {
                int sxi = sx >> 16;
                if (sxi >= src_w) sxi = src_w - 1;
                memcpy(dst_row + (size_t)x * bytes, src_row + (size_t)sxi * bytes, bytes);
                sx += step_x;
            }
        }
        sy += step_y;
    }
    return 0;
}

static int android_render_yv12_on_yv12(ANativeWindow_Buffer *out_buffer, const SDL_VoutOverlay *overlay)
{
    // SDLTRACE("SDL_VoutAndroid: android_render_yv12_on_yv12(%p)", overlay);
    assert(overlay->format == SDL_FCC_YV12);
    assert(overlay->planes == 3);

    if (out_buffer->width != overlay->w || out_buffer->height != overlay->h)
        return android_render_yv12_on_yv12_scaled(out_buffer, overlay);

    int min_height = IJKMIN(out_buffer->height, overlay->h);
    int dst_y_stride = out_buffer->stride;
    int dst_c_stride = IJKALIGN(out_buffer->stride / 2, 16);
    int dst_y_size = dst_y_stride * out_buffer->height;
    int dst_c_size = dst_c_stride * out_buffer->height / 2;

    // ALOGE("stride:%d/%d, size:%d/%d", dst_y_stride, dst_c_stride, dst_y_size, dst_c_size);

    uint8_t *dst_pixels_array[] = {
        out_buffer->bits,
        out_buffer->bits + dst_y_size,
        out_buffer->bits + dst_y_size + dst_c_size,
    };
    int dst_line_height[] = { min_height, min_height / 2, min_height / 2 };
    int dst_line_size_array[] = { dst_y_stride, dst_c_stride, dst_c_stride };

    for (int i = 0; i < 3; ++i) {
        int dst_line_size = dst_line_size_array[i];
        int src_line_size = overlay->pitches[i];
        int line_height = dst_line_height[i];
        uint8_t *dst_pixels = dst_pixels_array[i];
        const uint8_t *src_pixels = overlay->pixels[i];

        if (dst_line_size == src_line_size) {
            int plane_size = src_line_size * line_height;

            // ALOGE("sdl_image_copy_plane %p %p %d", dst_pixels, src_pixels, dst_plane_size);
            memcpy(dst_pixels, src_pixels, plane_size);
        } else {
            // TODO: 9 padding
            int bytewidth = IJKMIN(dst_line_size, src_line_size);

            // ALOGE("av_image_copy_plane %p %d %p %d %d %d", dst_pixels, dst_line_size, src_pixels, src_line_size, bytewidth, line_height);
            av_image_copy_plane(dst_pixels, dst_line_size, src_pixels, src_line_size, bytewidth, line_height);
        }
    }

    return 0;
}

static int android_render_on_yv12(ANativeWindow_Buffer *out_buffer, const SDL_VoutOverlay *overlay)
{
    assert(out_buffer);
    assert(overlay);

    switch (overlay->format) {
    case SDL_FCC_YV12: {
        return android_render_yv12_on_yv12(out_buffer, overlay);
    }
    }

    return -1;
}

static int android_render_rgb_on_rgb(ANativeWindow_Buffer *out_buffer, const SDL_VoutOverlay *overlay, int bpp)
{
    // SDLTRACE("SDL_VoutAndroid: android_render_rgb_on_rgb(%p)", overlay);
    assert(overlay->format == SDL_FCC_RV16);
    assert(overlay->planes == 1);

    if (out_buffer->width != overlay->w || out_buffer->height != overlay->h)
        return android_render_rgb_on_rgb_scaled(out_buffer, overlay, bpp);

    int min_height = IJKMIN(out_buffer->height, overlay->h);
    int dst_stride = out_buffer->stride;
    int src_line_size = overlay->pitches[0];
    int dst_line_size = dst_stride * bpp / 8;

    uint8_t *dst_pixels = out_buffer->bits;
    const uint8_t *src_pixels = overlay->pixels[0];

    if (dst_line_size == src_line_size) {
        int plane_size = src_line_size * min_height;
        // ALOGE("android_render_rgb_on_rgb (pix-match) %p %p %d", dst_pixels, src_pixels, plane_size);
        memcpy(dst_pixels, src_pixels, plane_size);
    } else {
        // TODO: 9 padding
        int bytewidth = IJKMIN(dst_line_size, src_line_size);

        // ALOGE("android_render_rgb_on_rgb (pix-mismatch) %p %d %p %d %d %d", dst_pixels, dst_line_size, src_pixels, src_line_size, bytewidth, min_height);
        av_image_copy_plane(dst_pixels, dst_line_size, src_pixels, src_line_size, bytewidth, min_height);
    }

    return 0;
}

static int android_render_rgb565_on_rgb565(ANativeWindow_Buffer *out_buffer, const SDL_VoutOverlay *overlay)
{
    return android_render_rgb_on_rgb(out_buffer, overlay, 16);
}

static int android_render_on_rgb565(ANativeWindow_Buffer *out_buffer, const SDL_VoutOverlay *overlay)
{
    assert(out_buffer);
    assert(overlay);

    switch (overlay->format) {
    case SDL_FCC_RV16: {
        return android_render_rgb565_on_rgb565(out_buffer, overlay);
    }
    }

    return -1;
}

static int android_render_rgb32_on_rgb8888(ANativeWindow_Buffer *out_buffer, const SDL_VoutOverlay *overlay)
{
    return android_render_rgb_on_rgb(out_buffer, overlay, 32);
}

static int android_render_on_rgb8888(ANativeWindow_Buffer *out_buffer, const SDL_VoutOverlay *overlay)
{
    assert(out_buffer);
    assert(overlay);

    switch (overlay->format) {
    case SDL_FCC_RV32: {
        return android_render_rgb32_on_rgb8888(out_buffer, overlay);
    }
    }

    return -1;
}

typedef struct AndroidHalFourccDescriptor {
    Uint32 fcc_or_hal;
    const char* name;

    int hal_format;

    int (*render)(ANativeWindow_Buffer *native_buffer, const SDL_VoutOverlay *overlay);
} AndroidHalFourccDescriptor;

static AndroidHalFourccDescriptor g_hal_fcc_map[] = {
    // YV12
    { HAL_PIXEL_FORMAT_YV12, "HAL_YV12", HAL_PIXEL_FORMAT_YV12, android_render_on_yv12 },
    { SDL_FCC_YV12, "YV12", HAL_PIXEL_FORMAT_YV12, android_render_on_yv12 },

    // RGB565
    { HAL_PIXEL_FORMAT_RGB_565, "HAL_RGB_565", HAL_PIXEL_FORMAT_RGB_565, android_render_on_rgb565 },
    { SDL_FCC_RV16, "RV16", HAL_PIXEL_FORMAT_RGB_565, android_render_on_rgb565 },

    // RGB8888
    { HAL_PIXEL_FORMAT_RGBX_8888, "HAL_RGBX_8888", HAL_PIXEL_FORMAT_RGBX_8888, android_render_on_rgb8888 },
    { HAL_PIXEL_FORMAT_RGBA_8888, "HAL_RGBA_8888", HAL_PIXEL_FORMAT_RGBA_8888, android_render_on_rgb8888 },
    { HAL_PIXEL_FORMAT_BGRA_8888, "HAL_BGRA_8888", HAL_PIXEL_FORMAT_BGRA_8888, android_render_on_rgb8888 },
    { SDL_FCC_RV32, "RV32", HAL_PIXEL_FORMAT_RGBX_8888, android_render_on_rgb8888 },
};

AndroidHalFourccDescriptor *native_window_get_desc(int fourcc_or_hal)
{
    for (int i = 0; i < NELEM(g_hal_fcc_map); ++i) {
        AndroidHalFourccDescriptor *desc = &g_hal_fcc_map[i];
        if (desc->fcc_or_hal == fourcc_or_hal)
            return desc;
    }

    return NULL;
}

int SDL_Android_NativeWindow_display_l(ANativeWindow *native_window, SDL_VoutOverlay *overlay)
{
    int retval;

    if (!native_window)
        return -1;

    if (!overlay) {
        ALOGE("SDL_Android_NativeWindow_display_l: NULL overlay");
        return -1;
    }

    if (overlay->w <= 0 || overlay->h <= 0) {
        ALOGE("SDL_Android_NativeWindow_display_l: invalid overlay dimensions(%d, %d)", overlay->w, overlay->h);
        return -1;
    }

    int curr_w = ANativeWindow_getWidth(native_window);
    int curr_h = ANativeWindow_getHeight(native_window);
    int curr_format = ANativeWindow_getFormat(native_window);
    int buff_w = IJKALIGN(overlay->w, 2);
    int buff_h = IJKALIGN(overlay->h, 2);

    AndroidHalFourccDescriptor *overlayDesc = native_window_get_desc(overlay->format);
    if (!overlayDesc) {
        ALOGE("SDL_Android_NativeWindow_display_l: unknown overlay format: %d", overlay->format);
        return -1;
    }

    AndroidHalFourccDescriptor *voutDesc = native_window_get_desc(curr_format);
    if (!voutDesc || voutDesc->hal_format != overlayDesc->hal_format) {
        // Keep an already-larger surface (aspect-fit view) instead of shrinking
        // it to the video size; the render path above scales the frame into it.
        int target_w = curr_w > buff_w ? curr_w : buff_w;
        int target_h = curr_h > buff_h ? curr_h : buff_h;
        ALOGD("ANativeWindow_setBuffersGeometry: w=%d, h=%d, f=%.4s(0x%x) => w=%d, h=%d, f=%.4s(0x%x)",
            curr_w, curr_h, (char*) &curr_format, curr_format,
            target_w, target_h, (char*) &overlay->format, overlay->format);
        retval = ANativeWindow_setBuffersGeometry(native_window, target_w, target_h, overlayDesc->hal_format);
        if (retval < 0) {
            ALOGE("SDL_Android_NativeWindow_display_l: ANativeWindow_setBuffersGeometry: failed %d", retval);
            return retval;
        }

        if (!voutDesc) {
            ALOGE("SDL_Android_NativeWindow_display_l: unknown hal format %d", curr_format);
            return -1;
        }
    }

    ANativeWindow_Buffer out_buffer;
    retval = ANativeWindow_lock(native_window, &out_buffer, NULL);
    if (retval < 0) {
        ALOGE("SDL_Android_NativeWindow_display_l: ANativeWindow_lock: failed %d", retval);
        return retval;
    }

    if (out_buffer.width != buff_w || out_buffer.height != buff_h) {
        // The app made the surface larger than the decoded frame on purpose
        // (aspect-fit view).  Keep that surface and let the render functions
        // scale into it; shrinking the geometry back here is what left low
        // resolution videos stuck in the top-left corner.
        if (out_buffer.width > buff_w || out_buffer.height > buff_h) {
            /* handled by the renderer's scaled path */
        } else {
            ALOGE("unexpected native window buffer (%p)(w:%d, h:%d, fmt:'%.4s'0x%x), expecting (w:%d, h:%d, fmt:'%.4s'0x%x)",
                native_window,
                out_buffer.width, out_buffer.height, (char*) &out_buffer.format, out_buffer.format,
                buff_w, buff_h, (char*) &overlay->format, overlay->format);
            // TODO: 8 set all black
            ANativeWindow_unlockAndPost(native_window);
            ANativeWindow_setBuffersGeometry(native_window, buff_w, buff_h, overlayDesc->hal_format);
            return -1;
        }
    }

    int render_ret = voutDesc->render(&out_buffer, overlay);
    if (render_ret < 0) {
        // TODO: 8 set all black
        // return after unlock image;
    }

    retval = ANativeWindow_unlockAndPost(native_window);
    if (retval < 0) {
        ALOGE("SDL_Android_NativeWindow_display_l: ANativeWindow_unlockAndPost: failed %d", retval);
        return retval;
    }

    return render_ret;
}
