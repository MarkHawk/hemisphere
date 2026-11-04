/*
 * bench.c  -  hemisphere-bench: how much work a Pi 3 can do and hold its
 * frame rate. A program of its own (make bench), not one of Hemisphere's
 * scenes, so it never shows in the catalog or the web remote. It uses the
 * same dome setup as Hemisphere: calibration.cfg, view.cfg (so the same
 * surface resolution) and the same surface and dots paths.
 *
 * One scene, BENCH, with two settings that pick the work:
 *   kind   what to ramp (see KINDS below)
 *   level  how much of it: a count for the shader kinds
 * Changing either rebuilds the surface shader with the level baked in as a
 * constant, as a real scene's GLSL would have it. tools/benchrun.py steps
 * through them over the control channel and reads the frame rate from
 * state, the same way tools/fpssweep.py does.
 *
 * Every step feeds its result into the colour, so the shader compiler
 * cannot drop the work. Settings are never saved unless you press S.
 */
#include "../dome/dome.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    K_BASE,      /* a plain surface: the cost of the dome itself        */
    K_SIN,       /* level sin() calls per pixel                         */
    K_TRIG,      /* level acos/atan pairs per pixel                     */
    K_CELLS,     /* level layers of hashed cells (floor + one h3 each)  */
    K_BAKED,     /* level lookups in a 256 x 256 tiling noise texture   */
    K_N3,        /* level octaves of real n3 value noise                */
    K_TEX,       /* level scattered lookups in a 2048 x 1024 texture    */
    K_THEME,     /* level theme() calls                                 */
    K_KINDS
};

static const char *KIND_NAMES[K_KINDS] = {
    "base", "sin", "acos/atan", "cells", "baked noise", "n3 octaves",
    "texture", "theme"
};

static struct { float kind, level; } L = { 0.0f, 1.0f };

static Param P[] = {
    { "kind",  &L.kind,  1.0f, 0.0f, K_KINDS - 1, 1,
      "0 base, 1 sin, 2 acos/atan, 3 cells, 4 baked noise, 5 n3 octaves, "
      "6 texture, 7 theme" },
    { "level", &L.level, 1.0f, 0.0f, 200.0f, 1, "how much of it: a count per pixel" },
};

/* one source for every kind; KIND and N are defined in front of it */
static const char *BENCH_GLSL =
    "uniform sampler2D u_noise, u_big;\n"
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  float t = u_time * 0.1;\n"
    "  float v = dir.x * 0.3 + dir.y * 0.2 + t;\n"
    "  vec3 c = vec3(0.0);\n"
    "#if KIND == 1\n"
    "  for (int i = 0; i < N; i++) { float fi = float(i);\n"
    "    v += sin(dot(dir, vec3(1.3 + fi * 0.37, 2.1 - fi * 0.23, 0.7 + fi * 0.11))\n"
    "             * (3.0 + fi) + t * (1.0 + 0.1 * fi)); }\n"
    "#elif KIND == 2\n"
    "  for (int i = 0; i < N; i++) { float fi = float(i);\n"
    "    float a = acos(clamp(dir.y * (1.0 - 0.02 * fi) + 0.01 * fi, -1.0, 1.0));\n"
    "    float b = atan(dir.x + 0.01 * fi, dir.z - 0.02 * fi);\n"
    "    v += a * 0.3 + b * 0.2; }\n"
    "#elif KIND == 3\n"
    "  for (int i = 0; i < N; i++) { float fi = float(i);\n"
    "    vec3 q = dir * (4.0 + 3.0 * fi) + vec3(t, 0.0, fi * 7.0);\n"
    "    vec3 f = fract(q) - 0.5;\n"
    "    v += h3(floor(q)) * (1.0 - smoothstep(0.2, 0.5, length(f))); }\n"
    "#elif KIND == 4\n"
    "  for (int i = 0; i < N; i++) { float fi = float(i);\n"
    "    v += texture2D(u_noise, dir.xz * (1.0 + 0.7 * fi) + vec2(0.13 * fi, t)).r; }\n"
    "#elif KIND == 5\n"
    "  vec3 q = dir * 2.0 + vec3(t); float a = 0.5;\n"
    "  for (int i = 0; i < N; i++) { float fi = float(i);\n"
    "    v += a * n3(q + fi * 11.0); q *= 2.03; a *= 0.5; }\n"
    "#elif KIND == 6\n"
    "  for (int i = 0; i < N; i++) { float fi = float(i);\n"
    "    v += texture2D(u_big, fract(dir.xy * (3.0 + 1.7 * fi) + vec2(0.37 * fi, t))).g; }\n"
    "#elif KIND == 7\n"
    "  for (int i = 0; i < N; i++) { float fi = float(i);\n"
    "    c += theme(mod(fi, 8.0), dir.x * (1.0 + fi) + dir.y * 0.5 + t); }\n"
    "  return vec4(c / float(N), 1.0);\n"
    "#endif\n"
    "  return vec4(fract(v) * vec3(1.0, 0.7, 0.4) + 0.1 * (dir * 0.5 + 0.5), 1.0);\n"
    "}\n";

static GLuint g_prog, g_noise_tex, g_big_tex;
static int    g_ready;

static unsigned hash_u(unsigned x)
{
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return x;
}

static GLuint tex_make(int w, int h, GLenum fmt, GLint wrap, const void *px)
{
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    glTexImage2D(GL_TEXTURE_2D, 0, (GLint)fmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, px);
    return t;
}

static int kind(void)  { int k = (int)lroundf(L.kind); return k < 0 ? 0 : k; }
static int level(void) { int n = (int)lroundf(L.level); return n < 1 ? 1 : n; }

static void rebuild(void)
{
    if (!g_ready) return;
    char *src = malloc(strlen(BENCH_GLSL) + 64);
    sprintf(src, "#define KIND %d\n#define N %d\n%s", kind(), level(), BENCH_GLSL);
    GLuint p = dome_surface_program_themed(src);
    free(src);
    if (!p) { printf("Bench: shader for kind %d level %d failed\n", kind(), level()); return; }
    if (g_prog) glDeleteProgram(g_prog);
    g_prog = p;
    printf("Bench: %s, level %d\n", kind() < K_KINDS ? KIND_NAMES[kind()] : "?", level());
    fflush(stdout);
}

static void init(void)
{
    /* the same sort of tiling noise music.c bakes: one byte per texel */
    unsigned char *n = malloc(256 * 256);
    for (int i = 0; i < 256 * 256; i++) n[i] = (unsigned char)(hash_u((unsigned)i) >> 24);
    g_noise_tex = tex_make(256, 256, GL_LUMINANCE, GL_REPEAT, n);
    free(n);
    /* a planet-map-sized RGBA texture, for lookups that miss the cache */
    unsigned *b = malloc((size_t)2048 * 1024 * 4);
    for (int i = 0; i < 2048 * 1024; i++) b[i] = hash_u((unsigned)i + 77u);
    g_big_tex = tex_make(2048, 1024, GL_RGBA, GL_CLAMP_TO_EDGE, b);
    free(b);
    g_ready = 1;
    rebuild();
}

static void draw(const DomeFrame *f)
{
    if (!g_prog) return;
    dome_surface_begin(g_prog, f);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, g_noise_tex);
    glUniform1i(glGetUniformLocation(g_prog, "u_noise"), 1);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, g_big_tex);
    glUniform1i(glGetUniformLocation(g_prog, "u_big"), 2);
    glActiveTexture(GL_TEXTURE0);
    dome_surface_end();
}

static Scene scene_bench = {
    "BENCH", "bench.cfg", 0, P, (int)(sizeof P / sizeof P[0]),
    init, rebuild, NULL, draw, NULL, NULL, NULL
};

int main(int argc, char **argv)
{
    Scene *const scenes[] = { &scene_bench };
    return dome_run(argc, argv, "Hemisphere bench", scenes, 1);
}
