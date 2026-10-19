/*
 * Copyright (c) 2026 ijkplayer legacy contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * OpenSL ES dynamic loader for the legacy (API 8) build. libOpenSLES.so only
 * exists since Android 2.3 (API 9), so the symbols are resolved at runtime
 * through dlopen(). On Android 2.1/2.2 dlopen fails, slCreateEngine() reports
 * SL_RESULT_FEATURE_UNSUPPORTED and the pipeline falls back to AudioTrack.
 */

#include <stdbool.h>
#include <string.h>
#include <dlfcn.h>
#include <pthread.h>

#include <SLES/OpenSLES.h>

#include "../ijksdl_log.h"

static pthread_once_t g_opensles_once = PTHREAD_ONCE_INIT;
static bool g_opensles_loaded = false;

static SLresult (*g_slCreateEngine)(SLObjectItf *pEngine, SLuint32 numOptions,
        const SLEngineOption *pEngineOptions, SLuint32 numInterfaces,
        const SLInterfaceID *pInterfaceIds, const SLboolean *pInterfaceRequired);

/* OpenSL compares interface IDs by value, so local copies of the UUIDs work. */
static struct SLInterfaceID_ g_iid_engine;
static struct SLInterfaceID_ g_iid_volume;
static struct SLInterfaceID_ g_iid_play;
static struct SLInterfaceID_ g_iid_androidsimplebufferqueue;

const struct SLInterfaceID_ * const SL_IID_ENGINE                   = &g_iid_engine;
const struct SLInterfaceID_ * const SL_IID_VOLUME                   = &g_iid_volume;
const struct SLInterfaceID_ * const SL_IID_PLAY                     = &g_iid_play;
const struct SLInterfaceID_ * const SL_IID_ANDROIDSIMPLEBUFFERQUEUE = &g_iid_androidsimplebufferqueue;

static void opensles_dynamic_init(void)
{
    void *handle = dlopen("libOpenSLES.so", RTLD_LAZY | RTLD_LOCAL);
    if (!handle) {
        ALOGE("opensles: dlopen libOpenSLES.so failed\n");
        return;
    }

    g_slCreateEngine = (SLresult (*)(SLObjectItf *, SLuint32, const SLEngineOption *,
            SLuint32, const SLInterfaceID *, const SLboolean *))
            dlsym(handle, "slCreateEngine");

    const struct SLInterfaceID_ **iids[] = {
        (const struct SLInterfaceID_ **)dlsym(handle, "SL_IID_ENGINE"),
        (const struct SLInterfaceID_ **)dlsym(handle, "SL_IID_VOLUME"),
        (const struct SLInterfaceID_ **)dlsym(handle, "SL_IID_PLAY"),
        (const struct SLInterfaceID_ **)dlsym(handle, "SL_IID_ANDROIDSIMPLEBUFFERQUEUE"),
    };
    struct SLInterfaceID_ *copies[] = {
        &g_iid_engine, &g_iid_volume, &g_iid_play, &g_iid_androidsimplebufferqueue,
    };

    bool ok = g_slCreateEngine != NULL;
    for (int i = 0; i < 4; ++i) {
        if (!iids[i]) {
            ok = false;
            break;
        }
        memcpy(copies[i], *iids[i], sizeof(struct SLInterfaceID_));
    }

    if (!ok) {
        ALOGE("opensles: required libOpenSLES symbols are unavailable\n");
        g_slCreateEngine = NULL;
        dlclose(handle);
        return;
    }

    g_opensles_loaded = true;
}

SLresult slCreateEngine(SLObjectItf *pEngine, SLuint32 numOptions,
        const SLEngineOption *pEngineOptions, SLuint32 numInterfaces,
        const SLInterfaceID *pInterfaceIds, const SLboolean *pInterfaceRequired)
{
    pthread_once(&g_opensles_once, opensles_dynamic_init);
    if (!g_opensles_loaded)
        return SL_RESULT_FEATURE_UNSUPPORTED;

    return g_slCreateEngine(pEngine, numOptions, pEngineOptions,
            numInterfaces, pInterfaceIds, pInterfaceRequired);
}
