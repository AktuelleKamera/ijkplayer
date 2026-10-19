/*
 * Dynamic EGL/GLES2 loader for the legacy (API 8) build. libEGL.so and
 * libGLESv2.so only exist since Android 2.3 (API 9); linking against them
 * would make libijksdl.so unloadable on 2.1/2.2. All entry points used by
 * ijksdl_egl.c and the gles2 renderers are resolved at runtime instead.
 *
 * Include this header AFTER <EGL/egl.h> / <GLES2/gl2.h>; every listed name
 * becomes a function pointer resolved from the system libraries.
 */

#ifndef IJKSDL_ANDROID__IJKSDL_EGL_GL_DYNAMIC_H
#define IJKSDL_ANDROID__IJKSDL_EGL_GL_DYNAMIC_H

#include <stdbool.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

#define IJK_EGL_DYN_LIST(X) \
    X(EGLDisplay,      eglGetDisplay,         (EGLNativeDisplayType)) \
    X(EGLBoolean,      eglInitialize,         (EGLDisplay, EGLint *, EGLint *)) \
    X(EGLBoolean,      eglTerminate,          (EGLDisplay)) \
    X(EGLBoolean,      eglChooseConfig,       (EGLDisplay, const EGLint *, EGLConfig *, EGLint, EGLint *)) \
    X(EGLBoolean,      eglGetConfigAttrib,    (EGLDisplay, EGLConfig, EGLint, EGLint *)) \
    X(EGLSurface,      eglCreateWindowSurface,(EGLDisplay, EGLConfig, EGLNativeWindowType, const EGLint *)) \
    X(EGLContext,      eglCreateContext,      (EGLDisplay, EGLConfig, EGLContext, const EGLint *)) \
    X(EGLBoolean,      eglMakeCurrent,        (EGLDisplay, EGLSurface, EGLSurface, EGLContext)) \
    X(EGLBoolean,      eglQuerySurface,       (EGLDisplay, EGLSurface, EGLint, EGLint *)) \
    X(EGLBoolean,      eglSwapBuffers,        (EGLDisplay, EGLSurface)) \
    X(EGLBoolean,      eglDestroySurface,     (EGLDisplay, EGLSurface)) \
    X(EGLBoolean,      eglDestroyContext,     (EGLDisplay, EGLContext)) \
    X(EGLBoolean,      eglReleaseThread,      (void)) \
    X(EGLint,          eglGetError,           (void))

#define IJK_GL_DYN_LIST(X) \
    X(const GLubyte *, glGetString,           (GLenum)) \
    X(GLenum,          glGetError,            (void)) \
    X(void,            glViewport,            (GLint, GLint, GLsizei, GLsizei)) \
    X(void,            glActiveTexture,       (GLenum)) \
    X(void,            glAttachShader,        (GLuint, GLuint)) \
    X(void,            glBindTexture,         (GLenum, GLuint)) \
    X(void,            glClear,               (GLbitfield)) \
    X(void,            glClearColor,          (GLclampf, GLclampf, GLclampf, GLclampf)) \
    X(void,            glCompileShader,       (GLuint)) \
    X(GLuint,          glCreateProgram,       (void)) \
    X(GLuint,          glCreateShader,        (GLenum)) \
    X(void,            glCullFace,            (GLenum)) \
    X(void,            glDeleteProgram,       (GLuint)) \
    X(void,            glDeleteShader,        (GLuint)) \
    X(void,            glDeleteTextures,      (GLsizei, const GLuint *)) \
    X(void,            glDisable,             (GLenum)) \
    X(void,            glDrawArrays,          (GLenum, GLint, GLsizei)) \
    X(void,            glEnable,              (GLenum)) \
    X(void,            glEnableVertexAttribArray, (GLuint)) \
    X(void,            glGenTextures,         (GLsizei, GLuint *)) \
    X(GLint,           glGetAttribLocation,   (GLuint, const GLchar *)) \
    X(void,            glGetProgramInfoLog,   (GLuint, GLsizei, GLsizei *, GLchar *)) \
    X(void,            glGetProgramiv,        (GLuint, GLenum, GLint *)) \
    X(void,            glGetShaderInfoLog,    (GLuint, GLsizei, GLsizei *, GLchar *)) \
    X(void,            glGetShaderiv,         (GLuint, GLenum, GLint *)) \
    X(GLint,           glGetUniformLocation,  (GLuint, const GLchar *)) \
    X(void,            glLinkProgram,         (GLuint)) \
    X(void,            glPixelStorei,         (GLenum, GLint)) \
    X(void,            glShaderSource,        (GLuint, GLsizei, const GLchar **, const GLint *)) \
    X(void,            glTexImage2D,          (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const GLvoid *)) \
    X(void,            glTexParameterf,       (GLenum, GLenum, GLfloat)) \
    X(void,            glTexParameteri,       (GLenum, GLenum, GLint)) \
    X(void,            glUniform1i,           (GLint, GLint)) \
    X(void,            glUniformMatrix3fv,    (GLint, GLsizei, GLboolean, const GLfloat *)) \
    X(void,            glUniformMatrix4fv,    (GLint, GLsizei, GLboolean, const GLfloat *)) \
    X(void,            glUseProgram,          (GLuint)) \
    X(void,            glVertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const GLvoid *))

/* ANativeWindow entries beyond the ones the legacy vout resolves itself. */
#define IJK_ANW_DYN_LIST(X) \
    X(int32_t,         ANativeWindow_getWidth,           (struct ANativeWindow *)) \
    X(int32_t,         ANativeWindow_getFormat,          (struct ANativeWindow *)) \
    X(int32_t,         ANativeWindow_setBuffersGeometry, (struct ANativeWindow *, int32_t, int32_t, int32_t))

#define IJK_DYN_DECL(ret, name, params) \
    extern ret (*ijk_dyn_##name) params;
IJK_EGL_DYN_LIST(IJK_DYN_DECL)
IJK_GL_DYN_LIST(IJK_DYN_DECL)
IJK_ANW_DYN_LIST(IJK_DYN_DECL)
#undef IJK_DYN_DECL

/*
 * Rename the canonical symbols to the dynamically resolved pointers. Must be
 * the last thing this header does so the declarations above keep their real
 * names.
 */
#define eglGetDisplay           ijk_dyn_eglGetDisplay
#define eglInitialize           ijk_dyn_eglInitialize
#define eglTerminate            ijk_dyn_eglTerminate
#define eglChooseConfig         ijk_dyn_eglChooseConfig
#define eglGetConfigAttrib      ijk_dyn_eglGetConfigAttrib
#define eglCreateWindowSurface  ijk_dyn_eglCreateWindowSurface
#define eglCreateContext        ijk_dyn_eglCreateContext
#define eglMakeCurrent          ijk_dyn_eglMakeCurrent
#define eglQuerySurface         ijk_dyn_eglQuerySurface
#define eglSwapBuffers          ijk_dyn_eglSwapBuffers
#define eglDestroySurface       ijk_dyn_eglDestroySurface
#define eglDestroyContext       ijk_dyn_eglDestroyContext
#define eglReleaseThread        ijk_dyn_eglReleaseThread
#define eglGetError             ijk_dyn_eglGetError

#define glGetString             ijk_dyn_glGetString
#define glGetError              ijk_dyn_glGetError
#define glViewport              ijk_dyn_glViewport
#define glActiveTexture         ijk_dyn_glActiveTexture
#define glAttachShader          ijk_dyn_glAttachShader
#define glBindTexture           ijk_dyn_glBindTexture
#define glClear                 ijk_dyn_glClear
#define glClearColor            ijk_dyn_glClearColor
#define glCompileShader         ijk_dyn_glCompileShader
#define glCreateProgram         ijk_dyn_glCreateProgram
#define glCreateShader          ijk_dyn_glCreateShader
#define glCullFace              ijk_dyn_glCullFace
#define glDeleteProgram         ijk_dyn_glDeleteProgram
#define glDeleteShader          ijk_dyn_glDeleteShader
#define glDeleteTextures        ijk_dyn_glDeleteTextures
#define glDisable               ijk_dyn_glDisable
#define glDrawArrays            ijk_dyn_glDrawArrays
#define glEnable                ijk_dyn_glEnable
#define glEnableVertexAttribArray ijk_dyn_glEnableVertexAttribArray
#define glGenTextures           ijk_dyn_glGenTextures
#define glGetAttribLocation     ijk_dyn_glGetAttribLocation
#define glGetProgramInfoLog     ijk_dyn_glGetProgramInfoLog
#define glGetProgramiv          ijk_dyn_glGetProgramiv
#define glGetShaderInfoLog      ijk_dyn_glGetShaderInfoLog
#define glGetShaderiv           ijk_dyn_glGetShaderiv
#define glGetUniformLocation    ijk_dyn_glGetUniformLocation
#define glLinkProgram           ijk_dyn_glLinkProgram
#define glPixelStorei           ijk_dyn_glPixelStorei
#define glShaderSource          ijk_dyn_glShaderSource
#define glTexImage2D            ijk_dyn_glTexImage2D
#define glTexParameterf         ijk_dyn_glTexParameterf
#define glTexParameteri         ijk_dyn_glTexParameteri
#define glUniform1i             ijk_dyn_glUniform1i
#define glUniformMatrix3fv      ijk_dyn_glUniformMatrix3fv
#define glUniformMatrix4fv      ijk_dyn_glUniformMatrix4fv
#define glUseProgram            ijk_dyn_glUseProgram
#define glVertexAttribPointer   ijk_dyn_glVertexAttribPointer

#define ANativeWindow_getWidth            ijk_dyn_ANativeWindow_getWidth
#define ANativeWindow_getFormat           ijk_dyn_ANativeWindow_getFormat
#define ANativeWindow_setBuffersGeometry  ijk_dyn_ANativeWindow_setBuffersGeometry

/* Resolve everything; safe to call multiple times. */
bool ijk_egl_gl_dynamic_init(void);
bool ijk_egl_gl_dynamic_ready(void);

#endif
