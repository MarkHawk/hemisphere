/*
 * glload.h  -  OpenGL ES 2.0 functions on every platform
 *
 * On the Pi (and any Linux) this just includes the system GLES2 header and
 * the functions come from libGLESv2. Windows has no OpenGL ES library, but
 * its graphics drivers (NVIDIA, Intel, AMD) offer the same functions, so on
 * Windows each gl... name below is a pointer, filled in by dome_gl_load()
 * once the window's context exists. Scenes call glUniform1f() and friends
 * exactly as before and never see the difference.
 *
 * A scene that needs a GL function not listed here: add it to the list in
 * DOME_GL_FUNCS and to the #define block below (one line each).
 */
#ifndef DOME_GLLOAD_H
#define DOME_GLLOAD_H

#ifdef _WIN32

#define GL_GLES_PROTOTYPES 0          /* types and constants, no prototypes */
#define SDL_USE_BUILTIN_OPENGL_DEFINITIONS 1   /* SDL's own copy of the headers */
#include <SDL2/SDL_opengles2.h>

#define DOME_GL_FUNCS(X) \
    X(PFNGLACTIVETEXTUREPROC, glActiveTexture) \
    X(PFNGLATTACHSHADERPROC, glAttachShader) \
    X(PFNGLBINDATTRIBLOCATIONPROC, glBindAttribLocation) \
    X(PFNGLBINDBUFFERPROC, glBindBuffer) \
    X(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer) \
    X(PFNGLBINDTEXTUREPROC, glBindTexture) \
    X(PFNGLBLENDFUNCPROC, glBlendFunc) \
    X(PFNGLBUFFERDATAPROC, glBufferData) \
    X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus) \
    X(PFNGLCLEARPROC, glClear) \
    X(PFNGLCLEARCOLORPROC, glClearColor) \
    X(PFNGLCOMPILESHADERPROC, glCompileShader) \
    X(PFNGLCREATEPROGRAMPROC, glCreateProgram) \
    X(PFNGLCREATESHADERPROC, glCreateShader) \
    X(PFNGLDISABLEPROC, glDisable) \
    X(PFNGLDISABLEVERTEXATTRIBARRAYPROC, glDisableVertexAttribArray) \
    X(PFNGLDRAWARRAYSPROC, glDrawArrays) \
    X(PFNGLENABLEPROC, glEnable) \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray) \
    X(PFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D) \
    X(PFNGLGENBUFFERSPROC, glGenBuffers) \
    X(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers) \
    X(PFNGLGENTEXTURESPROC, glGenTextures) \
    X(PFNGLGETERRORPROC, glGetError) \
    X(PFNGLGETINTEGERVPROC, glGetIntegerv) \
    X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog) \
    X(PFNGLGETPROGRAMIVPROC, glGetProgramiv) \
    X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog) \
    X(PFNGLGETSHADERIVPROC, glGetShaderiv) \
    X(PFNGLGETSTRINGPROC, glGetString) \
    X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation) \
    X(PFNGLLINKPROGRAMPROC, glLinkProgram) \
    X(PFNGLREADPIXELSPROC, glReadPixels) \
    X(PFNGLSHADERSOURCEPROC, glShaderSource) \
    X(PFNGLTEXIMAGE2DPROC, glTexImage2D) \
    X(PFNGLTEXPARAMETERIPROC, glTexParameteri) \
    X(PFNGLUNIFORM1FPROC, glUniform1f) \
    X(PFNGLUNIFORM1FVPROC, glUniform1fv) \
    X(PFNGLUNIFORM1IPROC, glUniform1i) \
    X(PFNGLUNIFORM2FPROC, glUniform2f) \
    X(PFNGLUNIFORM3FVPROC, glUniform3fv) \
    X(PFNGLUNIFORM4FVPROC, glUniform4fv) \
    X(PFNGLUNIFORMMATRIX3FVPROC, glUniformMatrix3fv) \
    X(PFNGLUSEPROGRAMPROC, glUseProgram) \
    X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer) \
    X(PFNGLVIEWPORTPROC, glViewport) \
    X(PFNGLDELETESHADERPROC, glDeleteShader) \
    X(PFNGLDELETEPROGRAMPROC, glDeleteProgram) \
    X(PFNGLDELETETEXTURESPROC, glDeleteTextures) \
    X(PFNGLDELETEBUFFERSPROC, glDeleteBuffers) \
    X(PFNGLTEXSUBIMAGE2DPROC, glTexSubImage2D) \
    X(PFNGLUNIFORM2FVPROC, glUniform2fv) \
    X(PFNGLUNIFORM3FPROC, glUniform3f) \
    X(PFNGLUNIFORM4FPROC, glUniform4f) \
    X(PFNGLUNIFORM2IPROC, glUniform2i) \
    X(PFNGLPIXELSTOREIPROC, glPixelStorei) \
    X(PFNGLGENERATEMIPMAPPROC, glGenerateMipmap) \

#define DOME_GL_DECLARE(type, name) extern type dome_##name;
DOME_GL_FUNCS(DOME_GL_DECLARE)
#undef DOME_GL_DECLARE

#define glActiveTexture              dome_glActiveTexture
#define glAttachShader               dome_glAttachShader
#define glBindAttribLocation         dome_glBindAttribLocation
#define glBindBuffer                 dome_glBindBuffer
#define glBindFramebuffer            dome_glBindFramebuffer
#define glBindTexture                dome_glBindTexture
#define glBlendFunc                  dome_glBlendFunc
#define glBufferData                 dome_glBufferData
#define glCheckFramebufferStatus     dome_glCheckFramebufferStatus
#define glClear                      dome_glClear
#define glClearColor                 dome_glClearColor
#define glCompileShader              dome_glCompileShader
#define glCreateProgram              dome_glCreateProgram
#define glCreateShader               dome_glCreateShader
#define glDisable                    dome_glDisable
#define glDisableVertexAttribArray   dome_glDisableVertexAttribArray
#define glDrawArrays                 dome_glDrawArrays
#define glEnable                     dome_glEnable
#define glEnableVertexAttribArray    dome_glEnableVertexAttribArray
#define glFramebufferTexture2D       dome_glFramebufferTexture2D
#define glGenBuffers                 dome_glGenBuffers
#define glGenFramebuffers            dome_glGenFramebuffers
#define glGenTextures                dome_glGenTextures
#define glGetError                   dome_glGetError
#define glGetIntegerv                dome_glGetIntegerv
#define glGetProgramInfoLog          dome_glGetProgramInfoLog
#define glGetProgramiv               dome_glGetProgramiv
#define glGetShaderInfoLog           dome_glGetShaderInfoLog
#define glGetShaderiv                dome_glGetShaderiv
#define glGetString                  dome_glGetString
#define glGetUniformLocation         dome_glGetUniformLocation
#define glLinkProgram                dome_glLinkProgram
#define glReadPixels                 dome_glReadPixels
#define glShaderSource               dome_glShaderSource
#define glTexImage2D                 dome_glTexImage2D
#define glTexParameteri              dome_glTexParameteri
#define glUniform1f                  dome_glUniform1f
#define glUniform1fv                 dome_glUniform1fv
#define glUniform1i                  dome_glUniform1i
#define glUniform2f                  dome_glUniform2f
#define glUniform3fv                 dome_glUniform3fv
#define glUniform4fv                 dome_glUniform4fv
#define glUniformMatrix3fv           dome_glUniformMatrix3fv
#define glUseProgram                 dome_glUseProgram
#define glVertexAttribPointer        dome_glVertexAttribPointer
#define glViewport                   dome_glViewport
#define glDeleteShader               dome_glDeleteShader
#define glDeleteProgram              dome_glDeleteProgram
#define glDeleteTextures             dome_glDeleteTextures
#define glDeleteBuffers              dome_glDeleteBuffers
#define glTexSubImage2D              dome_glTexSubImage2D
#define glUniform2fv                 dome_glUniform2fv
#define glUniform3f                  dome_glUniform3f
#define glUniform4f                  dome_glUniform4f
#define glUniform2i                  dome_glUniform2i
#define glPixelStorei                dome_glPixelStorei
#define glGenerateMipmap             dome_glGenerateMipmap

#else   /* Linux and the Pi */

#include <GLES2/gl2.h>

#endif

/* Fetch the functions from the current context (Windows); returns 0 and
 * prints which one is missing on failure. Does nothing elsewhere. */
int dome_gl_load(void);

#endif
