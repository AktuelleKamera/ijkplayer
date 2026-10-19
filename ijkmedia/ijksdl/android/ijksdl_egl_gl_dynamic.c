/*
 * Copyright (c) 2026 ijkplayer legacy contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Runtime resolver for the EGL/GLES2 symbols listed in
 * ijksdl_egl_gl_dynamic.h. Each symbol is searched in libEGL.so,
 * libGLESv2.so and libandroid.so; the first hit wins.
 */

#include <pthread.h>
#include <dlfcn.h>

#include "ijksdl_egl_gl_dynamic.h"
#include "../ijksdl_log.h"

static pthread_once_t g_once = PTHREAD_ONCE_INIT;
static bool g_ready = false;

/* storage for the function pointers declared (extern) in the header */
#define IJK_DYN_DEF(ret, name, params) ret (*ijk_dyn_##name) params;
IJK_EGL_DYN_LIST(IJK_DYN_DEF)
IJK_GL_DYN_LIST(IJK_DYN_DEF)
IJK_ANW_DYN_LIST(IJK_DYN_DEF)
#undef IJK_DYN_DEF

#define IJK_DYN_ADDR(ret, name, params) {#name, (void **)&ijk_dyn_##name},

static struct {
    const char *name;
    void **addr;
} g_dyn_table[] = {
    IJK_EGL_DYN_LIST(IJK_DYN_ADDR)
    IJK_GL_DYN_LIST(IJK_DYN_ADDR)
    IJK_ANW_DYN_LIST(IJK_DYN_ADDR)
};

static void egl_gl_dynamic_resolve(void)
{
    void *libs[3];
    libs[0] = dlopen("libEGL.so", RTLD_LAZY | RTLD_LOCAL);
    libs[1] = dlopen("libGLESv2.so", RTLD_LAZY | RTLD_LOCAL);
    libs[2] = dlopen("libandroid.so", RTLD_LAZY | RTLD_LOCAL);

    if (!libs[0] || !libs[1]) {
        ALOGI("eglgles-dyn: GLES libraries unavailable on this API level "
              "(EGL=%p GLESv2=%p); CPU rendering will be used\n", libs[0], libs[1]);
    }

    bool ok = true;
    size_t count = sizeof(g_dyn_table) / sizeof(g_dyn_table[0]);
    for (size_t i = 0; i < count; ++i) {
        void *sym = NULL;
        for (int l = 0; l < 3 && !sym; ++l) {
            if (libs[l])
                sym = dlsym(libs[l], g_dyn_table[i].name);
        }
        if (!sym) {
            ALOGW("eglgles-dyn: missing symbol %s\n", g_dyn_table[i].name);
            ok = false;
            continue;
        }
        *g_dyn_table[i].addr = sym;
    }

    /* The handles stay open on purpose: closing them could unload the
     * libraries and invalidate every resolved pointer. */
    g_ready = ok;
}

bool ijk_egl_gl_dynamic_init(void)
{
    pthread_once(&g_once, egl_gl_dynamic_resolve);
    return g_ready;
}

bool ijk_egl_gl_dynamic_ready(void)
{
    return g_ready;
}
