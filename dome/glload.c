/*
 * glload.c  -  fills in the GL function pointers on Windows (see glload.h)
 */
#include <SDL2/SDL.h>
#include "glload.h"
#include <stdio.h>

#ifdef _WIN32

#define DOME_GL_DEFINE(type, name) type dome_##name;
DOME_GL_FUNCS(DOME_GL_DEFINE)
#undef DOME_GL_DEFINE

int dome_gl_load(void)
{
    int ok = 1;
#define DOME_GL_FETCH(type, name)                                        \
    dome_##name = (type)SDL_GL_GetProcAddress(#name);                    \
    if (!dome_##name) { fprintf(stderr, "GL function missing: %s\n", #name); ok = 0; }
    DOME_GL_FUNCS(DOME_GL_FETCH)
#undef DOME_GL_FETCH
    return ok;
}

#else

int dome_gl_load(void) { return 1; }

#endif
