/*
 * Android 1.5 does not ship liblog.so (logging lived in libcutils back
 * then), so a legacy build must not carry a liblog dependency. This unit
 * DEFINES __android_log_print/__android_log_vprint and forwards them to
 * whichever system library provides them; messages are dropped when
 * neither exists.
 */

#if defined(__ANDROID__) && defined(IJK_ANDROID_LEGACY_SURFACE)

#include <stdbool.h>
#include <stdarg.h>
#include <pthread.h>
#include <dlfcn.h>

#include "../ijksdl_log.h"

typedef int (*log_print_fn)(int prio, const char *tag, const char *fmt, ...);
typedef int (*log_vprint_fn)(int prio, const char *tag, const char *fmt, va_list ap);

static pthread_once_t g_once = PTHREAD_ONCE_INIT;
static bool g_ready = false;
static log_vprint_fn g_vlog;

static void log_compat_resolve(void)
{
    const char *libs[] = { "liblog.so", "libcutils.so" };

    for (int l = 0; l < (int)(sizeof(libs) / sizeof(libs[0])); ++l) {
        void *handle = dlopen(libs[l], RTLD_LAZY | RTLD_LOCAL);
        if (!handle)
            continue;

        log_vprint_fn vprint =
            (log_vprint_fn)dlsym(handle, "__android_log_vprint");
        log_print_fn print =
            (log_print_fn)dlsym(handle, "__android_log_print");
        if (vprint && print) {
            g_vlog = vprint;
            /* keep the handle open on purpose */
            g_ready = true;
            return;
        }
    }

    ALOGE("log-compat: no native logging facility found\n");
}

int __android_log_vprint(int prio, const char *tag, const char *fmt, va_list ap)
{
    pthread_once(&g_once, log_compat_resolve);
    if (!g_ready)
        return 0;
    return g_vlog(prio, tag, fmt, ap);
}

int __android_log_print(int prio, const char *tag, const char *fmt, ...)
{
    pthread_once(&g_once, log_compat_resolve);
    if (!g_ready)
        return 0;

    va_list ap;
    va_start(ap, fmt);
    int ret = g_vlog(prio, tag, fmt, ap);
    va_end(ap);
    return ret;
}

#endif /* __ANDROID__ && IJK_ANDROID_LEGACY_SURFACE */
