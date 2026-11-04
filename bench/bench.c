/*
 * bench.c  -  hemisphere-bench: how much work a Pi 3 can do and hold its
 * frame rate. A program of its own (make bench), not one of Hemisphere's
 * scenes, so it never shows in the catalog or the web remote. It uses the
 * same dome setup as Hemisphere: calibration.cfg, view.cfg (so the same
 * surface resolution) and the same surface and dots paths.
 *
 * One scene, BENCH, with two settings that pick the work:
 *   kind   what to ramp (see the enum below)
 *   level  how much of it: a count per pixel for the shader kinds,
 *          thousands of dots, an upload size (1 to 7) or milliseconds
 * and one it only reports: cpu_ms, the CPU time of each frame's update
 * and draw (eased), so a slow step shows whether the CPU or the GPU ran
 * out.
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
    K_DOTS,      /* level thousand dots, moved, pushed and drawn         */
    K_UPLOAD,    /* one glTexSubImage2D a frame, size UPLOADS[level - 1] */
    K_BUSY,      /* level ms of busy loop on the main thread             */
    K_BRANCH,    /* 16 sins behind an if, taken by: 1 none (a setting),  */
                 /*   2 no pixel, 3 a 10% patch, 4 10% scattered, 5 all */
    K_MAP,       /* level smooth lookups in a 2048 x 1024 map, no mipmaps */
    K_MAPMIP,    /* the same with mipmaps                                */
    K_MAPS,      /* one lookup in each of level different 2048 x 1024 maps */
    K_MAPSMIP,   /* the same with mipmaps                                */
    K_FMT,       /* one lookup in each of 6 maps in one format: 1 luminance, */
                 /*   2 RGB, 3 RGBA, 4 luminance-alpha at 1024 x 512,     */
                 /*   5 luminance at 1137 x 728, 6 Weather's own mix      */
    K_KINDS
};

static const char *KIND_NAMES[K_KINDS] = {
    "base", "sin", "acos/atan", "cells", "baked noise", "n3 octaves",
    "texture", "theme", "dots", "upload", "busy", "branch", "map", "map mip",
    "maps", "maps mip", "formats"
};
#define N_MAPS 6

/* upload sizes; 0 x 0 means the whole screen */
static const int UPLOADS[][2] = {
    { 64, 64 }, { 128, 128 }, { 256, 256 }, { 512, 512 }, { 640, 474 },
    { 1024, 1024 }, { 0, 0 },
};
#define N_UPLOADS ((int)(sizeof UPLOADS / sizeof UPLOADS[0]))
#define MAX_DOTS  200000

static struct { float kind, level, cpu_ms; } L = { 0.0f, 1.0f, 0.0f };

static Param P[] = {
    { "kind",  &L.kind,  1.0f, 0.0f, K_KINDS - 1, 1,
      "0 base, 1 sin, 2 acos/atan, 3 cells, 4 baked noise, 5 n3 octaves, "
      "6 texture, 7 theme, 8 dots, 9 upload, 10 busy, 11 branch, 12 map, 13 map mip, 14 maps, 15 maps mip, 16 formats" },
    { "level", &L.level, 1.0f, 0.0f, 200.0f, 1,
      "how much: a count per pixel, thousands of dots, upload size 1 to 7, or ms" },
    { "cpu_ms", &L.cpu_ms, 0.1f, 0.0f, 1000.0f, 0,
      "reported, not set: CPU time of each frame's update and draw, ms (eased)" },
};

/* one source for every kind; KIND and N are defined in front of it */
static const char *BENCH_GLSL =
    "uniform sampler2D u_noise, u_big, u_up, u_map;\n"
    "uniform float u_off;\n"
    "uniform sampler2D u_m0, u_m1, u_m2, u_m3, u_m4, u_m5;\n"
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
    "#elif KIND == 9\n"
    "  v += texture2D(u_up, dir.xy * 0.5 + 0.5).r;\n"
    "#elif KIND == 11\n"
    /* who takes the branch; none of these can be worked out when compiling */
    "#if N == 1\n"
    "  bool take = u_off > 0.5;\n"                                  /* a setting, off */
    "#elif N == 2\n"
    "  bool take = view.z > 2.0;\n"                                 /* per pixel, never */
    "#elif N == 3\n"
    "  bool take = gl_FragCoord.y > 0.9 * u_size.y;\n"              /* the top tenth */
    "#elif N == 4\n"
    "  bool take = fract(gl_FragCoord.x * 0.1 + gl_FragCoord.y * 0.37) < 0.1;\n"   /* scattered */
    "#else\n"
    "  bool take = view.z > -2.0;\n"                                /* per pixel, always */
    "#endif\n"
    "  if (take) {\n"
    "    for (int i = 0; i < 16; i++) { float fi = float(i);\n"
    "      v += sin(dot(dir, vec3(1.3 + fi * 0.37, 2.1 - fi * 0.23, 0.7 + fi * 0.11))\n"
    "               * (3.0 + fi) + t * (1.0 + 0.1 * fi)); }\n"
    "  }\n"
    "#elif KIND == 12 || KIND == 13\n"
    /* lat/lon once, then lookups close together, as a planet map is read */
    "  vec2 uv = vec2(atan(dir.x, dir.z) / (2.0 * PI) + 0.5, acos(clamp(dir.y, -1.0, 1.0)) / PI);\n"
    "  for (int i = 0; i < N; i++) { float fi = float(i);\n"
    "    v += texture2D(u_map, uv + vec2(0.002 * fi, 0.001 * fi)).g; }\n"
    "#elif KIND == 14 || KIND == 15 || KIND == 16\n"
    /* the same point in several maps, as Weather reads day, night, clouds... */
    "  vec2 uv = vec2(atan(dir.x, dir.z) / (2.0 * PI) + 0.5, acos(clamp(dir.y, -1.0, 1.0)) / PI);\n"
    "#if KIND == 16\n#define M 6\n#else\n#define M N\n#endif\n"
    "  v += texture2D(u_m0, uv).r;\n"
    "#if M >= 2\n  v += texture2D(u_m1, uv).r;\n#endif\n"
    "#if M >= 3\n  v += texture2D(u_m2, uv).r;\n#endif\n"
    "#if M >= 4\n  v += texture2D(u_m3, uv).r;\n#endif\n"
    "#if M >= 5\n  v += texture2D(u_m4, uv).r;\n#endif\n"
    "#if M >= 6\n  v += texture2D(u_m5, uv).r;\n#endif\n"
    "#endif\n"
    "  return vec4(fract(v) * vec3(1.0, 0.7, 0.4) + 0.1 * (dir * 0.5 + 0.5), 1.0);\n"
    "}\n";

static GLuint g_prog, g_noise_tex, g_big_tex, g_up_tex, g_mip_tex;
static GLuint g_maps[N_MAPS], g_maps_mip[N_MAPS];   /* one byte a texel, 2 MB each */
static GLuint g_fmt[N_MAPS];                        /* the formats kind's, made on demand */
static int    g_fmt_level;
static int    g_ready;

static DomePoints g_dots;
static float     *g_dot_dir;            /* MAX_DOTS unit vectors, then colours */
static unsigned char *g_up_px;          /* the upload's pixels */
static int        g_up_w, g_up_h;
static Uint64     g_t0;                 /* when this frame's update began */

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

/* the six maps for the formats kind at this level, in the formats
 * Weather loads: day and night RGB, clouds luminance, the land mask
 * luminance-alpha at 1024 x 512, the local picture luminance at an
 * odd size */
static void fmt_make(int lv)
{
    if (lv > 6) lv = 6;
    if (lv == g_fmt_level) return;
    glDeleteTextures(N_MAPS, g_fmt);
    memset(g_fmt, 0, sizeof g_fmt);
    g_fmt_level = 0;
    unsigned char *px = malloc((size_t)2048 * 1024 * 4);
    if (!px) return;
    for (int m = 0; m < N_MAPS; m++) {
        static const GLenum MIX[N_MAPS] = { GL_RGB, GL_RGB, GL_LUMINANCE, GL_LUMINANCE,
                                            GL_LUMINANCE_ALPHA, GL_LUMINANCE };
        GLenum fmt = lv == 1 ? GL_LUMINANCE : lv == 2 ? GL_RGB : lv == 3 ? GL_RGBA
                   : lv == 4 ? GL_LUMINANCE_ALPHA : lv == 5 ? GL_LUMINANCE : MIX[m];
        int w = 2048, h = 1024;
        if (lv == 4 || (lv == 6 && m == 4)) { w = 1024; h = 512; }
        if (lv == 5 || (lv == 6 && m == 5)) { w = 1137; h = 728; }
        int bpp = fmt == GL_RGBA ? 4 : fmt == GL_RGB ? 3 : fmt == GL_LUMINANCE_ALPHA ? 2 : 1;
        for (size_t i = 0; i < (size_t)w * h * bpp; i++)
            px[i] = (unsigned char)(hash_u((unsigned)i * 5u + (unsigned)m) >> 24);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);      /* odd widths in RGB */
        g_fmt[m] = tex_make(w, h, fmt, GL_CLAMP_TO_EDGE, px);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    }
    free(px);
    g_fmt_level = lv;
    printf("Bench: formats case %d made\n", lv);
}

static void rebuild(void)
{
    if (!g_ready) return;
    if (kind() == K_FMT) fmt_make(level());
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
    /* the same pixels again with mipmaps, nearest level, as a planet map
     * could have them */
    g_mip_tex = tex_make(2048, 1024, GL_RGBA, GL_CLAMP_TO_EDGE, b);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
    for (int m = 0; m < N_MAPS; m++) {
        unsigned char *g = (unsigned char *)b;
        for (int i = 0; i < 2048 * 1024; i++) g[i] = (unsigned char)(hash_u((unsigned)i * 7u + (unsigned)m) >> 24);
        g_maps[m] = tex_make(2048, 1024, GL_LUMINANCE, GL_CLAMP_TO_EDGE, g);
        g_maps_mip[m] = tex_make(2048, 1024, GL_LUMINANCE, GL_CLAMP_TO_EDGE, g);
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
    }
    free(b);
    /* dots spread evenly over the sphere (a Fibonacci spiral), each with
     * a colour, as a particle scene would keep them */
    g_dot_dir = malloc(sizeof(float) * MAX_DOTS * 6);
    for (int i = 0; i < MAX_DOTS; i++) {
        float y = 1.0f - 2.0f * (i + 0.5f) / MAX_DOTS, r = sqrtf(1.0f - y * y);
        float a = 2.39996323f * i, *d = g_dot_dir + i * 6;
        d[0] = r * cosf(a); d[1] = y; d[2] = r * sinf(a);
        dome_theme_rgb(4, (float)i / 977.0f, d + 3);
    }
    dome_points_init(&g_dots, MAX_DOTS);
    g_ready = 1;
    rebuild();
}

static double ms_since(Uint64 t)
{
    return (double)(SDL_GetPerformanceCounter() - t) * 1000.0 / SDL_GetPerformanceFrequency();
}

static void update(const DomeFrame *f)
{
    g_t0 = SDL_GetPerformanceCounter();
    if (kind() == K_BUSY)
        while (ms_since(g_t0) < level()) {}
    if (kind() == K_DOTS) {
        /* four groups turning at their own rates: a few multiplies per
         * dot, as moving particles need */
        int n = level() * 1000;
        if (n > MAX_DOTS) n = MAX_DOTS;
        float c[4], s[4];
        for (int g = 0; g < 4; g++) {
            c[g] = cosf(f->now * (0.1f + 0.05f * g));
            s[g] = sinf(f->now * (0.1f + 0.05f * g));
        }
        dome_points_clear(&g_dots);
        for (int i = 0; i < n; i++) {
            const float *d = g_dot_dir + i * 6;
            int g = i & 3;
            dome_points_push(&g_dots, d[0] * c[g] - d[2] * s[g], d[1],
                             d[0] * s[g] + d[2] * c[g], d[3], d[4], d[5], 1.0f);
        }
    }
}

/* the texture for the upload kind, remade when the size changes */
static void upload(const DomeFrame *f)
{
    int i = level() - 1;
    if (i >= N_UPLOADS) i = N_UPLOADS - 1;
    int w = UPLOADS[i][0] ? UPLOADS[i][0] : f->width;
    int h = UPLOADS[i][1] ? UPLOADS[i][1] : f->height;
    if (w != g_up_w || h != g_up_h) {
        if (g_up_tex) glDeleteTextures(1, &g_up_tex);
        free(g_up_px);
        g_up_px = calloc((size_t)w * h, 4);
        g_up_tex = tex_make(w, h, GL_RGBA, GL_CLAMP_TO_EDGE, g_up_px);
        g_up_w = w; g_up_h = h;
        printf("Bench: uploading %d x %d a frame\n", w, h);
        fflush(stdout);
    }
    /* change a row each frame, so every upload carries new pixels */
    static unsigned frame;
    frame++;
    memset(g_up_px + (size_t)(frame % h) * w * 4, (int)(frame & 255), (size_t)w * 4);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, g_up_tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, g_up_px);
    glActiveTexture(GL_TEXTURE0);
}

static void draw(const DomeFrame *f)
{
    if (!g_prog) return;
    if (kind() == K_UPLOAD) upload(f);
    dome_surface_begin(g_prog, f);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, g_noise_tex);
    glUniform1i(glGetUniformLocation(g_prog, "u_noise"), 1);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, g_big_tex);
    glUniform1i(glGetUniformLocation(g_prog, "u_big"), 2);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, kind() == K_MAPMIP ? g_mip_tex : g_big_tex);
    glUniform1i(glGetUniformLocation(g_prog, "u_map"), 4);
    glUniform1f(glGetUniformLocation(g_prog, "u_off"), 0.0f);
    if (kind() == K_MAPS || kind() == K_MAPSMIP || kind() == K_FMT) {
        static const char *NAMES[N_MAPS] = { "u_m0", "u_m1", "u_m2", "u_m3", "u_m4", "u_m5" };
        for (int m = 0; m < N_MAPS; m++) {          /* units 8 to 13: 7 is the themes' */
            glActiveTexture(GL_TEXTURE8 + m);
            glBindTexture(GL_TEXTURE_2D, kind() == K_FMT ? g_fmt[m]
                          : kind() == K_MAPSMIP ? g_maps_mip[m] : g_maps[m]);
            glUniform1i(glGetUniformLocation(g_prog, NAMES[m]), 8 + m);
        }
    }
    if (g_up_tex) {
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, g_up_tex);
        glUniform1i(glGetUniformLocation(g_prog, "u_up"), 3);
    }
    glActiveTexture(GL_TEXTURE0);
    dome_surface_end();
    if (kind() == K_DOTS) {
        dome_points_upload(&g_dots);
        dome_draw_points(&g_dots, f->rot, 1.0f, 2.0f);
    }
    L.cpu_ms += ((float)ms_since(g_t0) - L.cpu_ms) * 0.05f;
}

static Scene scene_bench = {
    "BENCH", "bench.cfg", 0, P, (int)(sizeof P / sizeof P[0]),
    init, rebuild, update, draw, NULL, NULL, NULL
};

int main(int argc, char **argv)
{
    Scene *const scenes[] = { &scene_bench };
    return dome_run(argc, argv, "Hemisphere bench", scenes, 1);
}
