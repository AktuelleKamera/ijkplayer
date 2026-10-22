/* Android Surface backend covering API 3 through API 21+.
 * - API 9+:  public ANativeWindow API, resolved at runtime through
 *            dlopen("libandroid.so") because the legacy build does not link
 *            -landroid (the library does not exist before API 9).
 * - API <= 8: private android::Surface::lock/unlockAndPost, probed in
 *             libsurfaceflinger_client.so (2.0+) and libui.so (1.5/1.6).
 * MediaCodec (API 16+) renders directly into the Java Surface object and
 * never needs either backend. */

#include <assert.h>
#include <dlfcn.h>
#include <stdint.h>
#include <string.h>

#include "ijksdl_vout_android_surface.h"
#include "ijksdl_vout_android_nativewindow.h"
#include "ijksdl_aout_android_opensles.h"
#include "../ffmpeg/ijksdl_vout_overlay_ffmpeg.h"
#include "../ijksdl_vout_internal.h"
#include "../ijksdl_log.h"
#include "../ijksdl_container.h"
#include "../ijksdl_inc_internal.h"
#include "ijksdl_codec_android_mediacodec.h"
#include "ijksdl_vout_overlay_android_mediacodec.h"
#include "ijksdl_inc_internal_android.h"
#include "ijksdl_android_jni.h"
#include "../ijksdl_egl.h"
#include "../ijksdl_gles2.h"
#include "../ijksdl_fourcc.h"

typedef struct LegacySurfaceInfo {
    /* Raw lock() output; interpret per layout candidates:
     *   gingerbread+ : w h stride(px) usage format bits ...
     *   donut        : w h bpr(bytes) format bits base ...          */
    uint32_t v[8];
} LegacySurfaceInfo;

typedef int (*LegacySurfaceLock)(void *surface, LegacySurfaceInfo *info, int blocking);
typedef int (*LegacySurfaceUnlockAndPost)(void *surface);

/* ANativeWindow ABI (stable since API 9): */
typedef struct ANW_Buffer {
    int32_t width;
    int32_t height;
    int32_t stride; /* in pixels */
    int32_t format;
    void *bits;
} ANW_Buffer;

static struct {
    bool tried;
    bool ok;
    void *libandroid;
    void *(*fromSurface)(JNIEnv *env, jobject surface);
    void (*release)(void *window);
    int32_t (*lock)(void *window, ANW_Buffer *outBuffer, void *inOutDirtyBounds);
    int32_t (*unlockAndPost)(void *window);
} g_anw;

static bool anw_resolve(void)
{
    if (g_anw.tried)
        return g_anw.ok;
    g_anw.tried = true;

    g_anw.libandroid = dlopen("libandroid.so", RTLD_LAZY | RTLD_LOCAL);
    if (!g_anw.libandroid) {
        ALOGE("anw: dlopen libandroid.so failed: %s\n", dlerror());
        return false;
    }

    g_anw.fromSurface    = dlsym(g_anw.libandroid, "ANativeWindow_fromSurface");
    g_anw.release        = dlsym(g_anw.libandroid, "ANativeWindow_release");
    g_anw.lock           = dlsym(g_anw.libandroid, "ANativeWindow_lock");
    g_anw.unlockAndPost  = dlsym(g_anw.libandroid, "ANativeWindow_unlockAndPost");

    g_anw.ok = g_anw.fromSurface && g_anw.release && g_anw.lock && g_anw.unlockAndPost;
    if (!g_anw.ok)
        ALOGE("anw: required libandroid.so symbols are unavailable\n");
    return g_anw.ok;
}

struct SDL_AMediaCodecBufferProxy
{
    int buffer_id;
    int buffer_index;
    int acodec_serial;
    SDL_AMediaCodecBufferInfo buffer_info;
};

static void SDL_AMediaCodecBufferProxy_reset(SDL_AMediaCodecBufferProxy *proxy)
{
    memset(proxy, 0, sizeof(SDL_AMediaCodecBufferProxy));
    proxy->buffer_index  = -1;
    proxy->acodec_serial = 0;
}

static void SDL_AMediaCodecBufferProxy_init(SDL_AMediaCodecBufferProxy *proxy)
{
    assert(proxy);
    SDL_AMediaCodecBufferProxy_reset(proxy);
}

static void SDL_AMediaCodecBufferProxy_destroy(SDL_AMediaCodecBufferProxy *proxy)
{
    if (!proxy)
        return;
    SDL_AMediaCodecBufferProxy_reset(proxy);
}

static void SDL_AMediaCodecBufferProxy_destroyP(SDL_AMediaCodecBufferProxy **proxy)
{
    if (!proxy)
        return;
    SDL_AMediaCodecBufferProxy_destroy(*proxy);
    *proxy = NULL;
}

static void SDL_AMediaCodecBufferProxy_invalidate(SDL_AMediaCodecBufferProxy *proxy)
{
    SDL_AMediaCodecBufferProxy_reset(proxy);
}

typedef struct LegacyVoutOpaque {
    void *surface;              /* private Surface handle (API <= 8) */
    void *native_window;        /* ANativeWindow (API >= 9) */
    jobject surface_ref;        /* global ref of the Java Surface */

    SDL_AMediaCodec *acodec;
    int              next_buffer_id;

    IJK_EGL         *egl;

    ISDL_Array       overlay_manager;
    ISDL_Array       overlay_pool;
} LegacyVoutOpaque;

static SDL_Class g_legacy_vout_class = {
    .name = "AndroidLegacySurface_Vout",
};

/* android::Surface lives in libsurfaceflinger_client.so since Android 2.0;
 * on 1.5/1.6 it was part of libui.so, and some vendor builds kept it there.
 * Probe every known home and mangled form. All variants share the same
 * SurfaceInfo layout, so the blit path does not care which one hit. */
static const char *const g_legacy_libs[] = {
    "libsurfaceflinger_client.so",
    "libui.so",
};

static const char *const g_legacy_lock_syms[] = {
    "_ZN7android7Surface4lockEPNS0_11SurfaceInfoEb",  /* lock(SurfaceInfo *, bool) */
    "_ZN7android7Surface4lockEPNS0_11SurfaceInfoE",   /* lock(SurfaceInfo *)       */
};

static const char *const g_legacy_unlock_syms[] = {
    "_ZN7android7Surface13unlockAndPostEv",
};

struct LegacySurfaceApi {
    bool tried;
    bool ok;
    void *handle;
    LegacySurfaceLock lock;
    LegacySurfaceUnlockAndPost unlock_and_post;
};

static struct LegacySurfaceApi g_legacy_surface_api = { false, false, NULL, NULL, NULL };

static void legacy_resolve_surface_api_once(void)
{
    struct LegacySurfaceApi *api = &g_legacy_surface_api;

    for (int l = 0; l < (int)(sizeof(g_legacy_libs) / sizeof(g_legacy_libs[0])); ++l) {
        void *handle = dlopen(g_legacy_libs[l], RTLD_LAZY);
        if (!handle)
            continue;

        for (int i = 0; i < (int)(sizeof(g_legacy_lock_syms) / sizeof(g_legacy_lock_syms[0])); ++i) {
            LegacySurfaceLock lock = (LegacySurfaceLock)dlsym(handle, g_legacy_lock_syms[i]);
            if (!lock)
                continue;
            LegacySurfaceUnlockAndPost unlock =
                (LegacySurfaceUnlockAndPost)dlsym(handle, g_legacy_unlock_syms[0]);
            if (!unlock)
                continue;

            api->handle = handle;
            api->lock = lock;
            api->unlock_and_post = unlock;
            api->ok = true;
            ALOGI("legacy Surface: using %s (%s)\n", g_legacy_libs[l], g_legacy_lock_syms[i]);
            return;
        }
        /* keep the handle open on purpose: closing it could disturb others */
    }

    ALOGE("legacy Surface: no usable Surface::lock in libsurfaceflinger_client/libui; "
          "please provide both /system/lib files from this device");
}

static inline bool legacy_ensure_surface_api(void)
{
    if (!g_legacy_surface_api.tried) {
        g_legacy_surface_api.tried = true;
        legacy_resolve_surface_api_once();
    }
    return g_legacy_surface_api.ok;
}

static void legacy_free(SDL_Vout *vout)
{
    if (!vout)
        return;

    LegacyVoutOpaque *opaque = (LegacyVoutOpaque *)vout->opaque;
    if (opaque) {
        SDL_AMediaCodecBufferProxy **begin = (SDL_AMediaCodecBufferProxy **)ISDL_Array__begin(&opaque->overlay_manager);
        SDL_AMediaCodecBufferProxy **end   = (SDL_AMediaCodecBufferProxy **)ISDL_Array__end(&opaque->overlay_manager);
        for (; begin < end; ++begin) {
            SDL_AMediaCodecBufferProxy_destroyP(begin);
        }
        ISDL_Array__clear(&opaque->overlay_pool);
        ISDL_Array__clear(&opaque->overlay_manager);

        if (opaque->native_window && g_anw.ok) {
            g_anw.release(opaque->native_window);
            opaque->native_window = NULL;
        }

        IJK_EGL_freep(&opaque->egl);

        JNIEnv *env = NULL;
        if (opaque->surface_ref && SDL_JNI_SetupThreadEnv(&env) == JNI_OK)
            SDL_JNI_DeleteGlobalRefP(env, &opaque->surface_ref);

        SDL_AMediaCodec_decreaseReferenceP(&opaque->acodec);
    }
    SDL_Vout_FreeInternal(vout);
}

/* Shared pixel copy: overlay into a display buffer of any of the classic
 * Android RGB formats. format: RGBA=1, RGBX=2, RGB=3, RGB565=4, BGRA=5.
 *
 * Byte orders: overlay RV16 is RGB565; overlay RV32 is AV_PIX_FMT_0BGR32
 * which resolves to RGB0 on little-endian, i.e. memory order R,G,B,X -- the
 * same order Android's RGBA_8888 / RGBX_8888 windows expect (upstream
 * android_nativewindow.c simply memcpy()s between them). */
static void legacy_blit(SDL_VoutOverlay *overlay, int dst_w, int dst_h,
        uint32_t dst_pitch_units, bool pitch_is_bytes, uint32_t dst_format,
        uint8_t *dst)
{
    if (!overlay || !dst || dst_w <= 0 || dst_h <= 0 ||
            overlay->w <= 0 || overlay->h <= 0)
        return;

    const int src_w = overlay->w;
    const int src_h = overlay->h;
    const int dst_bpp = dst_format == 4 ? 2 : (dst_format == 3 ? 3 : 4);
    const int dst_pitch = pitch_is_bytes ? (int)dst_pitch_units
                                         : (int)(dst_pitch_units * dst_bpp);
    const uint8_t *src = overlay->pixels[0];
    const int src_pitch = overlay->pitches[0];

    if (dst_w == src_w && dst_h == src_h) {
        if (overlay->format == SDL_FCC_RV32 && (dst_format == 1 || dst_format == 2)) {
            /* identical byte order: straight row copy */
            for (int y = 0; y < dst_h; ++y)
                memcpy(dst + y * dst_pitch, src + y * src_pitch, dst_w * 4);
            return;
        }
        if (overlay->format == SDL_FCC_RV16 && dst_format == 4) {
            for (int y = 0; y < dst_h; ++y)
                memcpy(dst + y * dst_pitch, src + y * src_pitch, dst_w * 2);
            return;
        }
    }

    /* Surface is larger than the decoded frame (aspect-fit view): scale into
     * the whole surface instead of copying only the top-left video-sized box.
     * Old Froyo SurfaceView cannot scale the surface layer itself. */
    static int s_scale_logged = 0;
    if (!s_scale_logged) {
        s_scale_logged = 1;
        ALOGD("legacy_blit: scale %dx%d -> %dx%d", src_w, src_h, dst_w, dst_h);
    }
    int step_x = (int)(((int64_t)src_w << 16) / dst_w);
    int step_y = (int)(((int64_t)src_h << 16) / dst_h);
    int sy = 0;

    for (int y = 0; y < dst_h; ++y) {
        int syi = sy >> 16;
        if (syi >= src_h) syi = src_h - 1;
        const uint8_t *src_row = src + (size_t)syi * src_pitch;
        uint8_t *dst_row = dst + (size_t)y * dst_pitch;
        int sx = 0;
        for (int x = 0; x < dst_w; ++x) {
            int sxi = sx >> 16;
            if (sxi >= src_w) sxi = src_w - 1;
            if (overlay->format == SDL_FCC_RV32 && (dst_format == 1 || dst_format == 2)) {
                ((uint32_t *)dst_row)[x] = ((const uint32_t *)src_row)[sxi];
                sx += step_x;
                continue;
            }
            if (overlay->format == SDL_FCC_RV16 && dst_format == 4) {
                ((uint16_t *)dst_row)[x] = ((const uint16_t *)src_row)[sxi];
                sx += step_x;
                continue;
            }
            uint8_t r, g, b;
            if (overlay->format == SDL_FCC_RV16) {
                const uint16_t pixel = ((const uint16_t *)src_row)[sxi];
                r = (uint8_t)(((pixel >> 11) & 0x1f) << 3);
                g = (uint8_t)(((pixel >> 5) & 0x3f) << 2);
                b = (uint8_t)((pixel & 0x1f) << 3);
            } else {
                const uint8_t *s = src_row + (size_t)sxi * 4;
                r = s[0];
                g = s[1];
                b = s[2];
            }
            if (dst_format == 4) {
                uint16_t *pixel = (uint16_t *)(dst_row + (size_t)x * 2);
                *pixel = (uint16_t)(((r & 0xf8) << 8) |
                        ((g & 0xfc) << 3) | (b >> 3));
            } else if (dst_format == 3) {
                uint8_t *pixel = dst_row + (size_t)x * 3;
                pixel[0] = r;
                pixel[1] = g;
                pixel[2] = b;
            } else if (dst_format == 5) {
                uint8_t *pixel = dst_row + (size_t)x * 4;
                pixel[0] = b;
                pixel[1] = g;
                pixel[2] = r;
                pixel[3] = 0xff;
            } else {
                uint8_t *pixel = dst_row + (size_t)x * 4;
                pixel[0] = r;
                pixel[1] = g;
                pixel[2] = b;
                pixel[3] = dst_format == 1 ? 0xff : 0;
            }
            sx += step_x;
        }
        sy += step_y;
    }
}

static int legacy_display_l(SDL_Vout *vout, SDL_VoutOverlay *overlay)
{
    LegacyVoutOpaque *opaque = (LegacyVoutOpaque *)vout->opaque;

    if (overlay && overlay->format == SDL_FCC__AMC) {
        /* MediaCodec frame: the Java decoder renders it into the configured
         * Surface directly; only release the buffer here. */
        return SDL_VoutOverlayAMediaCodec_releaseFrame_l(overlay, NULL, true);
    }

    if (!opaque || !overlay)
        return -1;

    switch (overlay->format) {
    case SDL_FCC_RV24:
    case SDL_FCC_I420:
    case SDL_FCC_I444P10LE: {
        /* only GLES support */
        if (opaque->egl && ijk_egl_gl_dynamic_ready())
            return IJK_EGL_display(opaque->egl, opaque->native_window, overlay);
        break;
    }
    case SDL_FCC_YV12:
    case SDL_FCC_RV16:
    case SDL_FCC_RV32: {
        /* both GLES & CPU blit support */
        if (vout->overlay_format == SDL_FCC__GLES2 &&
                opaque->egl && ijk_egl_gl_dynamic_ready())
            return IJK_EGL_display(opaque->egl, opaque->native_window, overlay);
        break;
    }
    default:
        break;
    }

    if (overlay->format != SDL_FCC_RV16 && overlay->format != SDL_FCC_RV32)
        return -1;

    /* falling back to CPU blit: drop any live EGL context first */
    IJK_EGL_terminate(opaque->egl);

    /* API 9+: public ANativeWindow path. */
    if (opaque->native_window && g_anw.ok) {
        ANW_Buffer buf;
        memset(&buf, 0, sizeof(buf));
        if (g_anw.lock(opaque->native_window, &buf, NULL) != 0 || !buf.bits)
            return -1;

        if (buf.format != 1 && buf.format != 2 && buf.format != 3 &&
                buf.format != 4) {
            ALOGE("anw: unsupported format %d", buf.format);
            g_anw.unlockAndPost(opaque->native_window);
            return -1;
        }
        legacy_blit(overlay, buf.width, buf.height, (uint32_t)buf.stride,
                false, (uint32_t)buf.format, (uint8_t *)buf.bits);
        return g_anw.unlockAndPost(opaque->native_window);
    }

    /* API <= 8: private Surface::lock backend.
     * SurfaceInfo layout differs across releases:
     *   Donut (1.5/1.6): { w, h, bpr(bytes), format, bits, base, ... }
     *   Eclair..Froyo..: { w, h, stride(px), usage, format, bits, ... }
     * The lock fills the same memory either way; decode it under each
     * candidate and keep the one that yields sane values. */
    if (!opaque->surface)
        return -1;

    if (!legacy_ensure_surface_api())
        return -1;

    LegacySurfaceInfo info;
    memset(&info, 0, sizeof(info));
    if (g_legacy_surface_api.lock(opaque->surface, &info, 1) != 0)
        return -1;

    static int s_layout = -1; /* -1 unknown; 0 gingerbread+; 1 donut */
    const uint32_t *f = info.v;
    uint32_t sw = f[0], sh = f[1];
    uint32_t sfmt = 0;
    void *sbits = NULL;
    uint32_t spitch = 0;
    bool pitch_bytes = false;

    for (int pass = 0; pass < 2 && sfmt == 0; ++pass) {
        int layout = s_layout >= 0 ? s_layout : (pass == 0 ? 0 : 1);
        uint32_t cand_fmt;
        void *cand_bits;
        uint32_t cand_pitch;
        bool cand_bytes;

        if (layout == 1) {
            cand_fmt = f[3];
            cand_bits = *(void **)&f[4];
            cand_pitch = f[2];          /* bytes per row */
            cand_bytes = true;
            if ((int32_t)f[2] < (int32_t)sw)
                continue;
        } else {
            cand_fmt = f[4];
            cand_bits = *(void **)&f[5];
            cand_pitch = f[2];          /* pixels per row */
            cand_bytes = false;
            if (f[2] < sw || f[2] > sw * 4 + 64)
                continue;
        }

        if (sw < 1 || sw > 4096 || sh < 1 || sh > 4096)
            continue;
        if (cand_fmt < 1 || cand_fmt > 5 || !cand_bits)
            continue;

        sfmt = cand_fmt;
        sbits = cand_bits;
        spitch = cand_pitch;
        pitch_bytes = cand_bytes;
        if (s_layout < 0) {
            s_layout = layout;
            ALOGI("legacy Surface: SurfaceInfo layout %s\n",
                    layout == 1 ? "donut(w,h,bpr,format,bits)" : "eclair+(w,h,stride,usage,format,bits)");
        }
    }

    if (sfmt == 0) {
        ALOGE("legacy Surface: undecodable SurfaceInfo "
              "{%08x %08x %08x %08x %08x %08x}",
              f[0], f[1], f[2], f[3], f[4], f[5]);
        g_legacy_surface_api.unlock_and_post(opaque->surface);
        return -1;
    }

    legacy_blit(overlay, (int)sw, (int)sh, spitch, pitch_bytes, sfmt,
            (uint8_t *)sbits);

    return g_legacy_surface_api.unlock_and_post(opaque->surface);
}

static int legacy_display(SDL_Vout *vout, SDL_VoutOverlay *overlay)
{
    SDL_LockMutex(vout->mutex);
    int retval = legacy_display_l(vout, overlay);
    SDL_UnlockMutex(vout->mutex);
    return retval;
}

static SDL_VoutOverlay *legacy_create_overlay_l(int width, int height, int frame_format, SDL_Vout *vout)
{
    switch (frame_format) {
    case IJK_AV_PIX_FMT__ANDROID_MEDIACODEC:
        return SDL_VoutAMediaCodec_CreateOverlay(width, height, vout);
    default:
        return SDL_VoutFFmpeg_CreateOverlay(width, height, frame_format, vout);
    }
}

static SDL_VoutOverlay *legacy_create_overlay(int width, int height, int frame_format, SDL_Vout *vout)
{
    SDL_LockMutex(vout->mutex);
    SDL_VoutOverlay *overlay = legacy_create_overlay_l(width, height, frame_format, vout);
    SDL_UnlockMutex(vout->mutex);
    return overlay;
}

SDL_Vout *SDL_VoutAndroid_CreateForAndroidSurface(void)
{
    SDL_Vout *vout = SDL_Vout_CreateInternal(sizeof(LegacyVoutOpaque));
    if (!vout)
        return NULL;

    LegacyVoutOpaque *opaque = (LegacyVoutOpaque *)vout->opaque;
    if (ISDL_Array__init(&opaque->overlay_manager, 32))
        goto fail;
    if (ISDL_Array__init(&opaque->overlay_pool, 32))
        goto fail;

    ijk_egl_gl_dynamic_init();
    opaque->egl = IJK_EGL_create();
    if (!opaque->egl)
        goto fail;

    vout->opaque_class    = &g_legacy_vout_class;
    vout->create_overlay  = legacy_create_overlay;
    vout->free_l          = legacy_free;
    vout->display_overlay = legacy_display;
    vout->overlay_format  = SDL_FCC_RV32;
    return vout;
fail:
    legacy_free(vout);
    return NULL;
}

/* OpenSL ES is resolved dynamically (ijksdl_opensles_dynamic.c): real output
 * on API 9+, AudioTrack fallback on 2.1/2.2. */

void SDL_VoutAndroid_SetAndroidSurface(JNIEnv *env, SDL_Vout *vout, jobject android_surface)
{
    if (!env || !vout)
        return;

    LegacyVoutOpaque *opaque = (LegacyVoutOpaque *)vout->opaque;
    SDL_VoutAndroid_invalidateAllBuffers(vout);

    if (opaque->native_window && g_anw.ok) {
        g_anw.release(opaque->native_window);
        opaque->native_window = NULL;
    }
    IJK_EGL_terminate(opaque->egl);
    if (opaque->surface_ref) {
        SDL_JNI_DeleteGlobalRefP(env, &opaque->surface_ref);
        opaque->surface_ref = NULL;
    }
    opaque->surface = NULL;

    if (!android_surface)
        return;

    opaque->surface_ref = (*env)->NewGlobalRef(env, android_surface);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        opaque->surface_ref = NULL;
        ALOGE("legacy Surface: NewGlobalRef failed");
        return;
    }

    if (SDL_Android_GetApiLevel() >= IJK_API_9_GINGERBREAD && anw_resolve()) {
        opaque->native_window = g_anw.fromSurface(env, android_surface);
        if (!opaque->native_window)
            ALOGE("legacy Surface: ANativeWindow_fromSurface failed");
        return;
    }

    /* API <= 8: read the private int handle for Surface::lock. */
    jclass surface_class = (*env)->GetObjectClass(env, android_surface);
    jfieldID surface_field = (*env)->GetFieldID(env, surface_class, "mSurface", "I");
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        ALOGE("legacy Surface: mSurface field unavailable");
        return;
    }
    opaque->surface = (void *)(intptr_t)(*env)->GetIntField(env, android_surface, surface_field);
    legacy_ensure_surface_api();
}

static void SDL_VoutAndroid_invalidateAllBuffers_l(SDL_Vout *vout)
{
    LegacyVoutOpaque *opaque = (LegacyVoutOpaque *)vout->opaque;

    SDL_AMediaCodecBufferProxy **begin = (SDL_AMediaCodecBufferProxy **)ISDL_Array__begin(&opaque->overlay_manager);
    SDL_AMediaCodecBufferProxy **end   = (SDL_AMediaCodecBufferProxy **)ISDL_Array__end(&opaque->overlay_manager);
    for (; begin < end; ++begin) {
        SDL_AMediaCodecBufferProxy_invalidate(*begin);
    }
}

void SDL_VoutAndroid_invalidateAllBuffers(SDL_Vout *vout)
{
    SDL_LockMutex(vout->mutex);
    SDL_VoutAndroid_invalidateAllBuffers_l(vout);
    SDL_UnlockMutex(vout->mutex);
}

static void SDL_VoutAndroid_setAMediaCodec_l(SDL_Vout *vout, SDL_AMediaCodec *acodec)
{
    LegacyVoutOpaque *opaque = (LegacyVoutOpaque *)vout->opaque;
    if (opaque->acodec == acodec)
        return;

    SDL_VoutAndroid_invalidateAllBuffers_l(vout);

    SDL_AMediaCodec_decreaseReferenceP(&opaque->acodec);
    opaque->acodec = acodec;
    if (opaque->acodec)
        SDL_AMediaCodec_increaseReference(opaque->acodec);
}

void SDL_VoutAndroid_setAMediaCodec(SDL_Vout *vout, SDL_AMediaCodec *acodec)
{
    SDL_LockMutex(vout->mutex);
    SDL_VoutAndroid_setAMediaCodec_l(vout, acodec);
    SDL_UnlockMutex(vout->mutex);
}

SDL_AMediaCodec *SDL_VoutAndroid_peekAMediaCodec(SDL_Vout *vout)
{
    LegacyVoutOpaque *opaque = (LegacyVoutOpaque *)vout->opaque;
    SDL_AMediaCodec *acodec = NULL;

    SDL_LockMutex(vout->mutex);
    acodec = opaque->acodec;
    SDL_UnlockMutex(vout->mutex);
    return acodec;
}

static SDL_AMediaCodecBufferProxy *SDL_VoutAndroid_obtainBufferProxy_l(SDL_Vout *vout, int acodec_serial, int buffer_index, SDL_AMediaCodecBufferInfo *buffer_info)
{
    LegacyVoutOpaque *opaque = (LegacyVoutOpaque *)vout->opaque;
    SDL_AMediaCodecBufferProxy *proxy = NULL;

    if (ISDL_Array__size(&opaque->overlay_pool) > 0) {
        proxy = ISDL_Array__pop_back(&opaque->overlay_pool);
        SDL_AMediaCodecBufferProxy_reset(proxy);
    } else {
        proxy = (SDL_AMediaCodecBufferProxy *)mallocz(sizeof(SDL_AMediaCodecBufferProxy));
        if (!proxy)
            return NULL;
        SDL_AMediaCodecBufferProxy_init(proxy);
        ISDL_Array__push_back(&opaque->overlay_manager, proxy);
    }

    proxy->buffer_id     = opaque->next_buffer_id++;
    proxy->acodec_serial = acodec_serial;
    proxy->buffer_index  = buffer_index;
    proxy->buffer_info   = *buffer_info;
    return proxy;
}

SDL_AMediaCodecBufferProxy *SDL_VoutAndroid_obtainBufferProxy(SDL_Vout *vout, int acodec_serial, int buffer_index, SDL_AMediaCodecBufferInfo *buffer_info)
{
    SDL_AMediaCodecBufferProxy *proxy = NULL;
    SDL_LockMutex(vout->mutex);
    proxy = SDL_VoutAndroid_obtainBufferProxy_l(vout, acodec_serial, buffer_index, buffer_info);
    SDL_UnlockMutex(vout->mutex);
    return proxy;
}

static int SDL_VoutAndroid_releaseBufferProxy_l(SDL_Vout *vout, SDL_AMediaCodecBufferProxy *proxy, bool render)
{
    LegacyVoutOpaque *opaque = (LegacyVoutOpaque *)vout->opaque;

    if (!proxy)
        return 0;

    ISDL_Array__push_back(&opaque->overlay_pool, proxy);

    if (!SDL_AMediaCodec_isSameSerial(opaque->acodec, proxy->acodec_serial)) {
        ALOGW("%s: stale proxy %d idx: %d\n", __func__, proxy->buffer_id, proxy->buffer_index);
        return 0;
    }

    if (proxy->buffer_index < 0) {
        ALOGE("%s: invalid AMediaCodec buffer index %d\n", __func__, proxy->buffer_index);
        return 0;
    } else if (proxy->buffer_info.flags & AMEDIACODEC__BUFFER_FLAG_FAKE_FRAME) {
        proxy->buffer_index = -1;
        return 0;
    }

    sdl_amedia_status_t amc_ret = SDL_AMediaCodec_releaseOutputBuffer(opaque->acodec, proxy->buffer_index, render);
    if (amc_ret != SDL_AMEDIA_OK) {
        ALOGW("%s: releaseOutputBuffer(%d, %d) failed %d\n", __func__, proxy->buffer_index, render, (int)amc_ret);
        proxy->buffer_index = -1;
        return -1;
    }
    proxy->buffer_index = -1;

    return 0;
}

int SDL_VoutAndroid_releaseBufferProxyP(SDL_Vout *vout, SDL_AMediaCodecBufferProxy **proxy, bool render)
{
    int ret = 0;

    if (!proxy)
        return 0;

    SDL_LockMutex(vout->mutex);
    ret = SDL_VoutAndroid_releaseBufferProxy_l(vout, *proxy, render);
    SDL_UnlockMutex(vout->mutex);
    *proxy = NULL;
    return ret;
}

int SDL_VoutAndroid_releaseBufferProxyP_l(SDL_Vout *vout, SDL_AMediaCodecBufferProxy **proxy, bool render)
{
    int ret = 0;

    if (!proxy)
        return 0;

    ret = SDL_VoutAndroid_releaseBufferProxy_l(vout, *proxy, render);
    *proxy = NULL;
    return ret;
}
