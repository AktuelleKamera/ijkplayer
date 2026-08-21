/* Android 2.1/2.2 Surface backend. The public ANativeWindow API starts at API 9. */

#include "ijksdl_vout_android_surface.h"
#include "ijksdl_vout_android_nativewindow.h"
#include "ijksdl_aout_android_opensles.h"
#include "../ffmpeg/ijksdl_vout_overlay_ffmpeg.h"
#include "../ijksdl_vout_internal.h"
#include "../ijksdl_log.h"

#include <dlfcn.h>
#include <stdint.h>
#include <string.h>

typedef struct LegacySurfaceInfo {
    uint32_t w;
    uint32_t h;
    uint32_t stride;
    uint32_t usage;
    uint32_t format;
    void *bits;
    uint32_t reserved[2];
} LegacySurfaceInfo;

typedef int (*LegacySurfaceLock)(void *surface, LegacySurfaceInfo *info, int blocking);
typedef int (*LegacySurfaceUnlockAndPost)(void *surface);

typedef struct LegacyVoutOpaque {
    void *surface;
    void *libui;
    LegacySurfaceLock lock;
    LegacySurfaceUnlockAndPost unlock_and_post;
} LegacyVoutOpaque;

static SDL_Class g_legacy_vout_class = {
    .name = "AndroidLegacySurface_Vout",
};

static void legacy_resolve_surface_api(LegacyVoutOpaque *opaque)
{
    if (opaque->libui)
        return;

    opaque->libui = dlopen("libui.so", RTLD_LAZY);
    if (!opaque->libui) {
        ALOGE("legacy Surface: cannot load libui.so");
        return;
    }

    opaque->lock = (LegacySurfaceLock)dlsym(
            opaque->libui,
            "_ZN7android7Surface4lockEPNS0_11SurfaceInfoEb");
    opaque->unlock_and_post = (LegacySurfaceUnlockAndPost)dlsym(
            opaque->libui,
            "_ZN7android7Surface13unlockAndPostEv");
    if (!opaque->lock || !opaque->unlock_and_post)
        ALOGE("legacy Surface: required libui symbols are unavailable");
}

static void legacy_free(SDL_Vout *vout)
{
    if (!vout)
        return;

    LegacyVoutOpaque *opaque = (LegacyVoutOpaque *)vout->opaque;
    if (opaque && opaque->libui)
        dlclose(opaque->libui);
    SDL_Vout_FreeInternal(vout);
}

static int legacy_display(SDL_Vout *vout, SDL_VoutOverlay *overlay)
{
    LegacyVoutOpaque *opaque = (LegacyVoutOpaque *)vout->opaque;
    if (!opaque || !opaque->surface || !overlay || overlay->format != SDL_FCC_RV16)
        return -1;

    legacy_resolve_surface_api(opaque);
    if (!opaque->lock || !opaque->unlock_and_post)
        return -1;

    LegacySurfaceInfo info;
    memset(&info, 0, sizeof(info));
    if (opaque->lock(opaque->surface, &info, 1) != 0 || !info.bits)
        return -1;

    /* RGB_565 is HAL format 4 and uses a 16-bit pixel. */
    if (info.format != 4) {
        ALOGE("legacy Surface: unsupported format %u (RGB565 required)", info.format);
        opaque->unlock_and_post(opaque->surface);
        return -1;
    }

    const int width = overlay->w < (int)info.w ? overlay->w : (int)info.w;
    const int height = overlay->h < (int)info.h ? overlay->h : (int)info.h;
    const int src_pitch = overlay->pitches[0];
    const int dst_pitch = info.stride * 2;
    uint8_t *src = overlay->pixels[0];
    uint8_t *dst = (uint8_t *)info.bits;
    for (int y = 0; y < height; ++y)
        memcpy(dst + y * dst_pitch, src + y * src_pitch, width * 2);

    return opaque->unlock_and_post(opaque->surface);
}

SDL_Vout *SDL_VoutAndroid_CreateForAndroidSurface(void)
{
    SDL_Vout *vout = SDL_Vout_CreateInternal(sizeof(LegacyVoutOpaque));
    if (!vout)
        return NULL;

    vout->opaque_class = &g_legacy_vout_class;
    vout->create_overlay = SDL_VoutFFmpeg_CreateOverlay;
    vout->free_l = legacy_free;
    vout->display_overlay = legacy_display;
    vout->overlay_format = SDL_FCC_RV16;
    return vout;
}

/* API 7 has no OpenSL ES library. The pipeline falls back to AudioTrack. */
SDL_Aout *SDL_AoutAndroid_CreateForOpenSLES(void)
{
    return NULL;
}

bool SDL_AoutAndroid_IsObjectOfOpenSLES(SDL_Aout *aout)
{
    (void)aout;
    return false;
}

void SDL_VoutAndroid_SetAndroidSurface(JNIEnv *env, SDL_Vout *vout, jobject android_surface)
{
    if (!env || !vout)
        return;

    LegacyVoutOpaque *opaque = (LegacyVoutOpaque *)vout->opaque;
    opaque->surface = NULL;
    if (!android_surface)
        return;

    jclass surface_class = (*env)->GetObjectClass(env, android_surface);
    jfieldID surface_field = (*env)->GetFieldID(env, surface_class, "mSurface", "I");
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        ALOGE("legacy Surface: mSurface field unavailable");
        return;
    }
    opaque->surface = (void *)(intptr_t)(*env)->GetIntField(env, android_surface, surface_field);
    legacy_resolve_surface_api(opaque);
}

/* Hardware MediaCodec is unavailable on API 7. Keep the symbols as no-ops so
 * the common pipeline can still be linked while software decoding is forced. */
void SDL_VoutAndroid_setAMediaCodec(SDL_Vout *vout, SDL_AMediaCodec *acodec) { (void)vout; (void)acodec; }
SDL_AMediaCodec *SDL_VoutAndroid_peekAMediaCodec(SDL_Vout *vout) { (void)vout; return NULL; }
void SDL_VoutAndroid_invalidateAllBuffers(SDL_Vout *vout) { (void)vout; }
SDL_AMediaCodecBufferProxy *SDL_VoutAndroid_obtainBufferProxy(SDL_Vout *vout, int serial, int index, SDL_AMediaCodecBufferInfo *info)
{ (void)vout; (void)serial; (void)index; (void)info; return NULL; }
int SDL_VoutAndroid_releaseBufferProxyP(SDL_Vout *vout, SDL_AMediaCodecBufferProxy **proxy, bool render)
{ (void)vout; (void)render; if (proxy) *proxy = NULL; return 0; }
int SDL_VoutAndroid_releaseBufferProxyP_l(SDL_Vout *vout, SDL_AMediaCodecBufferProxy **proxy, bool render)
{ return SDL_VoutAndroid_releaseBufferProxyP(vout, proxy, render); }
