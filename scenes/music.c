/*
 * music.c  -  Music: built for sound, from WLED audio sync (dome/audio.h).
 * N steps through the styles, M through the colour themes:
 *
 *   0 spectrum ring   16 bars standing in from the rim, bass at the bottom,
 *                     treble at the top, mirrored left and right; at full
 *                     volume they nearly meet in the middle
 *   1 pulse globe     a glowing ball in the middle that swells with the
 *                     bass and the beat, with a halo from the treble
 *   2 ripples         every snare hit sends a ring out from the middle
 *   3 starfield       stars streaming out from the middle, faster with
 *                     the bass, stretching into streaks at speed (dots)
 *   4 aurora          curtains of light across the top of the dome,
 *                     swaying in time and further with the snare body,
 *                     their tops lifting in colour with the treble
 *   5 radial bars     64 thin bars standing out from the middle, bright
 *                     tips glowing a little more on the beat
 *   6 oscilloscope    a trace round the rim bent by the bands, with
 *                     fading after-images
 *   7 lissajous       a turning figure, a quarter turn a beat, sized by
 *                     the bass, morphing to a new ratio every 20 s
 *   8 wire tunnel     rings flying out of the middle on the bass, bending
 *                     with it and bulging on the beat
 *   9 terrain         a wireframe landscape of spectra scrolling towards
 *                     you below a horizon
 *
 * With no music every style has a calm, slow idle of its own, and the
 * change between idle and music is eased over about a second.
 *
 * NO FLASHING: a beat never lights the whole dome. Beats swell sizes, move
 * things and light small areas, always eased (u_swell, never the raw
 * beat), and sound changes the overall brightness by 15% at most.
 *
 * HOW IT IS BUILT, for speed on the Pi 3
 *   Each style is a row in STYLES: its own small surface shader (one
 *   program per style, so no pixel pays for another style's code), and
 *   optional CPU hooks for its own state, uniforms and dots. Every shader
 *   gets the same small library (MUSIC_LIB) and the shared textures: the
 *   spectrum, a tileable noise and a per-style texture. On the VC4, trig
 *   (acos, atan, sin) is what costs, so per pixel a style keeps to about
 *   one acos, one atan, a few lookups and one theme() call; anything that
 *   depends only on uniforms (a colour, a sin of the time) is worked out
 *   on the CPU, using the theme colours read back from the GPU at start.
 */
#include "../dome/dome.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct { float style, audio, react, speed, theme, bright; } L =
    { 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f };

/* ---- what every style's shader gets ------------------------------- */
/* u_bass..u_live come from dome_audio_uniforms, already faded by the
 * scene's gain; u_swell is the beat with its leap eased; u_t is the
 * scene's clock, u_flow a clock that runs faster with the bass; u_c0 and
 * u_c1 are colours from the CPU, u_k0 and u_k1 numbers, each style's own.
 * u_spec is the 16 bands, u_noise a tileable noise (two octaves, 0..1),
 * u_tex the style's own texture. plane() is a flat, trig-free position
 * across the dome: 0 in the middle, about 1 at the rim. */
static const char MUSIC_LIB[] =
    "uniform float u_t, u_flow, u_bright, u_theme, u_react, u_swell, u_k0, u_k1;\n"
    "uniform float u_bass, u_body, u_mid, u_air, u_level, u_phase, u_live;\n"
    "uniform vec3  u_c0, u_c1;\n"
    "uniform sampler2D u_spec, u_noise, u_tex;\n"
    "vec2 plane(vec3 v){ return v.xy / (1.0 + max(v.z, 0.0)); }\n"
    "float noise(vec2 p){ return texture2D(u_noise, p).r; }\n"
    /* cheap acos and atan: the VC4 builds the real ones from long
     * sequences. facos is good to about 0.0001, fatan2 to 0.0001 rad. */
    "float facos(float x){\n"
    "  float a = abs(x);\n"
    "  float r = sqrt(1.0 - a) * (1.5707288 + a * (-0.2121144 + a * (0.0742610 - 0.0187293 * a)));\n"
    "  return x < 0.0 ? 3.1415927 - r : r;\n"
    "}\n"
    "float fatan2(float y, float x){\n"
    "  float ax = abs(x), ay = abs(y);\n"
    "  float a = min(ax, ay) / max(max(ax, ay), 1e-6), s = a * a;\n"
    "  float r = ((-0.0464964749 * s + 0.15931422) * s - 0.327622764) * s * a + a;\n"
    "  if (ay > ax) r = 1.5707963 - r;\n"
    "  if (x < 0.0) r = 3.1415927 - r;\n"
    "  return y < 0.0 ? -r : r;\n"
    "}\n";

static float  g_t, g_flow;           /* the scene's clocks */
static float  g_gain;                /* eases to the audio setting */
static float  g_swell;               /* the beat, with its leap eased */
static float  g_live;                /* music arriving, times the gain */

/* ---- small helpers ------------------------------------------------ */
static float hash3i(int x, int y, int z)
{
    unsigned h = (unsigned)x * 73856093u ^ (unsigned)y * 19349663u ^ (unsigned)z * 83492791u;
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return (float)(h & 0xffffffu) / 16777215.0f;
}

static float smooth01(float a, float b, float x)
{
    float t = (x - a) / (b - a);
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    return t * t * (3.0f - 2.0f * t);
}

static float ease_to(float v, float target, float tau, float dt)
{
    return v + (target - v) * (1.0f - expf(-dt / tau));
}

static void set1(GLuint p, const char *name, float v) { glUniform1f(glGetUniformLocation(p, name), v); }
static void set3(GLuint p, const char *name, const float *v) { glUniform3fv(glGetUniformLocation(p, name), 1, v); }

/* ---- the colour themes on the CPU --------------------------------- */
/* theme(id, x) for every theme at 256 steps of x, drawn once by the GPU
 * and read back, so dots and per-frame colours match the shaders exactly */
#define THEME_STEPS 256
static unsigned char g_themes[DOME_THEMES][THEME_STEPS][4];

static void themes_build(void)
{
    static const char VS[] =
        "attribute vec2 a_pos;\n"
        "void main(){ gl_Position = vec4(a_pos * 2.0 - 1.0, 0.0, 1.0); }\n";
    static const char FS_MAIN[] =
        "void main(){\n"
        "  float x = floor(gl_FragCoord.x) / 256.0, id = floor(gl_FragCoord.y);\n"
        "  gl_FragColor = vec4(clamp(theme(id, x), 0.0, 1.0), 1.0);\n"
        "}\n";
    size_t n = strlen(DOME_GLSL_THEMES) + sizeof FS_MAIN + 64;
    char *fs = malloc(n);
    snprintf(fs, n, "precision mediump float;\n%s%s", DOME_GLSL_THEMES, FS_MAIN);
    GLuint prog = dome_compile(VS, fs);
    free(fs);

    GLuint tex, fbo, vbo;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, THEME_STEPS, DOME_THEMES, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    static const float QUAD[8] = { 0, 0, 1, 0, 0, 1, 1, 1 };
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof QUAD, QUAD, GL_STATIC_DRAW);
    glViewport(0, 0, THEME_STEPS, DOME_THEMES);
    glDisable(GL_BLEND);
    glUseProgram(prog);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (void *)0);
    glEnableVertexAttribArray(0);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glReadPixels(0, 0, THEME_STEPS, DOME_THEMES, GL_RGBA, GL_UNSIGNED_BYTE, g_themes);
    /* the tiny framebuffer is kept: the Windows GL loader has no
     * glDeleteFramebuffers, and it is made only once */
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteTextures(1, &tex);
    glDeleteBuffers(1, &vbo);
    glDeleteProgram(prog);
}

/* the current theme at x (cyclic), 0..1 */
static void theme_rgb(float x, float out[3])
{
    int id = (int)lroundf(L.theme);
    id = id < 0 ? 0 : id >= DOME_THEMES ? DOME_THEMES - 1 : id;
    x -= floorf(x);
    float fx = x * THEME_STEPS;
    int i = (int)fx, j = (i + 1) % THEME_STEPS;
    float t = fx - (float)i;
    i %= THEME_STEPS;
    for (int k = 0; k < 3; k++)
        out[k] = (g_themes[id][i][k] * (1.0f - t) + g_themes[id][j][k] * t) / 255.0f;
}

/* ---- textures ----------------------------------------------------- */
static GLuint tex_new(int w, int h, GLenum fmt, GLint wrap, const void *px)
{
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    glTexImage2D(GL_TEXTURE_2D, 0, fmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, px);
    return t;
}

/* upload a row of 0..1 values into a 1-pixel-high luminance texture */
static void tex_row(GLuint t, const float *v, int n)
{
    unsigned char px[256];
    for (int i = 0; i < n && i < 256; i++)
        px[i] = (unsigned char)lroundf(fminf(1.0f, fmaxf(0.0f, v[i])) * 255.0f);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, n, 1, GL_LUMINANCE, GL_UNSIGNED_BYTE, px);
}

#define NOISE_N 256
static GLuint g_spec_tex, g_noise_tex;

/* tileable value noise, two octaves, so styles get smooth noise from one
 * lookup instead of hashing with sin per pixel */
static GLuint noise_build(void)
{
    unsigned char *px = malloc(NOISE_N * NOISE_N);
    static const int CELLS[2] = { 8, 32 };
    static const float AMP[2] = { 0.68f, 0.32f };
    for (int y = 0; y < NOISE_N; y++)
        for (int x = 0; x < NOISE_N; x++) {
            float v = 0.0f;
            for (int o = 0; o < 2; o++) {
                int c = CELLS[o];
                float fx = (float)x * c / NOISE_N, fy = (float)y * c / NOISE_N;
                int ix = (int)fx, iy = (int)fy;
                float tx = fx - ix, ty = fy - iy;
                tx = tx * tx * (3 - 2 * tx); ty = ty * ty * (3 - 2 * ty);
                float a = hash3i(ix % c, iy % c, o), b = hash3i((ix + 1) % c, iy % c, o);
                float d = hash3i(ix % c, (iy + 1) % c, o), e = hash3i((ix + 1) % c, (iy + 1) % c, o);
                v += AMP[o] * ((a + (b - a) * tx) * (1 - ty) + (d + (e - d) * tx) * ty);
            }
            px[y * NOISE_N + x] = (unsigned char)lroundf(v * 255.0f);
        }
    GLuint t = tex_new(NOISE_N, NOISE_N, GL_LUMINANCE, GL_REPEAT, px);
    free(px);
    return t;
}

/* ---- dots ---------------------------------------------------------- */
/* One pool of dots in three sizes, shared by every style that draws dots
 * (only one style is on screen at a time). Most dots live in dome space,
 * placed by plane position: plane_dir() is the inverse of the shaders'
 * plane(), so (0, 0) is the middle of the dome and radius 1 the rim. */
#define POOL_N 30000
enum { SMALL, MID, LARGE };
static DomePoints g_pool[3];

static void plane_dir(float x, float y, float out[3])
{
    float r2 = x * x + y * y, k = 1.0f / (1.0f + r2);
    out[0] = 2.0f * x * k;
    out[1] = 2.0f * y * k;
    out[2] = (1.0f - r2) * k;
}

static void pool_clear(void) { for (int b = 0; b < 3; b++) dome_points_clear(&g_pool[b]); }

static void dot_plane(int b, float x, float y, const float c[3], float a)
{
    float d[3];
    plane_dir(x, y, d);
    dome_points_push(&g_pool[b], d[0], d[1], d[2], c[0], c[1], c[2], a);
}

/* a line between two plane positions, as dots on the sphere */
static void line_plane(int b, float x0, float y0, float x1, float y1, const float c[3], float a)
{
    float d0[3], d1[3];
    plane_dir(x0, y0, d0);
    plane_dir(x1, y1, d1);
    dome_points_line(&g_pool[b], d0, d1, c[0], c[1], c[2], a, 0.007f);
}

/* draw the pool: fixed to the dome unless a rotation is given */
static void pool_draw(const DomeFrame *f, const float *rot, float s0, float s1, float s2)
{
    mat3 id;
    m3_identity(id);
    const float size[3] = { s0, s1, s2 };
    for (int b = 0; b < 3; b++) {
        dome_points_upload(&g_pool[b]);
        dome_draw_points(&g_pool[b], rot ? rot : id, f->fade * fminf(1.0f, L.bright), size[b]);
    }
}

/* the beat arriving: true on the frame its eased swell starts rising */
static float g_prev_beat;
static int    g_beat_now;

/* ================================================================== */
/* The styles                                                          */
/* ================================================================== */

/* ---- 0: spectrum ring ---- */
static const char RING[] =
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  float phi = facos(clamp(view.z, -1.0, 1.0));\n"    /* 0 centre, ~1.57 rim */
    /* round the dome: 0 at the bottom, 1 at the top, the same both sides */
    "  float b = abs(fatan2(view.x, -view.y)) / PI;\n"
    "  float x = b * 16.0, i = floor(x), fx = fract(x);\n"
    "  float v = texture2D(u_spec, vec2((i + 0.5) / 16.0, 0.5)).r;\n"
    /* with no music, a slow faint wave runs round the rim */
    "  float idle = 0.10 + 0.06 * sin(b * 9.0 - u_t * 0.8);\n"
    /* each band is scaled to its own recent loudest, so everyday levels sit
     * low; the square root lifts them, and full volume still reaches 1 */
    "  v = max(sqrt(min(v * u_react, 1.0)), idle * (1.0 - u_live));\n"
    "  float h = 0.06 + 1.42 * v;\n"                      /* bar height: nearly to the middle */
    "  float in_bar = smoothstep(0.08, 0.16, fx) * smoothstep(0.92, 0.84, fx);\n"
    "  float d = phi - (PI * 0.5 - h);\n"                 /* > 0 inside the bar */
    "  float bar = smoothstep(-0.01, 0.01, d) * in_bar;\n"
    /* bright tip, and a soft glow beyond it that swells on the beat */
    "  float tq = max(0.0, 1.0 - d * d * 450.0);\n"
    "  float tip = tq * tq * in_bar;\n"
    "  float glow = exp(-max(-d, 0.0) * 9.0) * (0.10 + 0.25 * u_swell * u_react) * in_bar;\n"
    "  vec3 c = theme(u_theme, b * 0.7 + u_t * 0.01);\n"
    "  vec3 col = c * (bar * (0.6 + 0.4 * d / h) + glow) + vec3(1.0) * tip * 0.35 * bar;\n"
    "  return vec4(col * u_bright, 1.0);\n"
    "}\n";

/* ---- 1: pulse globe. Kept lean: the ball's size is measured as
 * length(view.xy) (no acos), and the breath and background colour come
 * from the CPU (u_k0, u_c0). ---- */
static const char PULSE[] =
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  float r = length(view.xy);\n"
    /* the ball's size: bass, the beat's swell, and a slow breath when idle */
    "  float R = 0.37 + u_react * (0.20 * u_bass + 0.11 * u_swell) + u_k0;\n"
    "  float k = r / R;\n"
    "  float ball = sqrt(max(0.0, 1.0 - k * k));\n"        /* round shading */
    "  float edge = smoothstep(1.0, 0.97, k);\n"
    "  float halo = exp(-max(r - R, 0.0) * 7.0) * (1.0 - edge)\n"
    "             * (0.18 + 0.30 * u_air * u_react);\n"
    /* colour drifts slowly round the theme, the core paler */
    "  vec3 c = theme(u_theme, u_t * 0.02 + k * 0.15);\n"
    "  float b2 = ball * ball;\n"
    "  vec3 col = (c * (0.25 + 0.75 * ball) + mix(c, vec3(1.0), 0.6) * b2 * b2 * b2 * 0.35) * edge\n"
    "           + c * halo;\n"
    /* faint rings in the background, turning slowly */
    "  col += u_c0 * 0.04 * (0.5 + 0.5 * sin(r * 14.0 - u_t * 0.5)) * (1.0 - edge);\n"
    "  return vec4(col * u_bright, 1.0);\n"
    "}\n";

static void pulse_uniforms(GLuint p, const DomeFrame *f)
{
    (void)f;
    float c[3];
    theme_rgb(0.5f + g_t * 0.01f, c);
    set3(p, "u_c0", c);
    set1(p, "u_k0", (1.0f - g_live) * 0.05f * sinf(g_t * 0.63f));   /* 6 breaths a minute */
}

/* ---- 2: ripples. The CPU keeps the last few seconds of beats in a
 * 256x1 texture, newest at the left, shifted along as time passes; read
 * at the angle from the middle, each beat is a ring moving out. ---- */
static const char RIPPLES[] =
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  float phi = acos(clamp(view.z, -1.0, 1.0));\n"
    "  float u = phi / (PI * 0.5);\n"
    "  float r = texture2D(u_tex, vec2(0.01 + 0.98 * u, 0.5)).r * (1.0 - 0.55 * u);\n"
    "  vec3 c = theme(u_theme, u * 0.4 + u_t * 0.015);\n"
    "  vec3 col = c * r * 1.2 + vec3(1.0) * r * r * r * 0.35;\n"
    /* a soft standing glow in the middle from the overall level */
    "  col += c * exp(-phi * 4.0) * (0.06 + 0.18 * u_level * u_react);\n"
    /* and very faint fixed rings, so the dome is never quite empty */
    "  col += u_c0 * 0.025 * (0.5 + 0.5 * cos(phi * 20.0));\n"
    "  return vec4(col * u_bright, 1.0);\n"
    "}\n";

#define RIP_N    256                 /* texels in the ripple history */
#define RIP_SECS 3.0f                /* seconds for a ring to reach the rim */
static GLuint g_rip_tex;
static float  g_rip[RIP_N];          /* newest first */
static float  g_rip_acc;

static void ripples_update(const DomeFrame *f)
{
    /* what enters the middle now: the eased beat, weighted by the snare
     * body; with no music, a soft ring every 4.5 seconds */
    float idle = fmodf(g_t, 4.5f) - 0.2f;
    float in = g_swell * L.react * (0.55f + 0.45f * f->audio.body * g_gain)
             + (1.0f - g_live) * 0.45f * expf(-idle * idle / 0.012f);
    float step = RIP_SECS / (RIP_N * L.speed);
    for (g_rip_acc += f->dt; g_rip_acc >= step; g_rip_acc -= step) {
        memmove(g_rip + 1, g_rip, (RIP_N - 1) * sizeof g_rip[0]);
        g_rip[0] = fminf(1.0f, in);
    }
}

static void ripples_uniforms(GLuint p, const DomeFrame *f)
{
    (void)f;
    float c[3];
    theme_rgb(0.5f, c);
    set3(p, "u_c0", c);
    glActiveTexture(GL_TEXTURE3);
    tex_row(g_rip_tex, g_rip, RIP_N);
}

/* ---- 3: starfield. The stars are dots (stars_points); the surface only
 * adds a faint glow at the vanishing point. Per-pixel stars cost too much
 * trig on the Pi. ---- */
static const char STARS[] =
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  float r = length(view.xy);\n"
    "  vec3 col = u_c0 * exp(-r * 6.0) * 0.08 * step(0.0, view.z);\n"
    "  return vec4(col * u_bright, 1.0);\n"
    "}\n";

static void stars_uniforms(GLuint p, const DomeFrame *f)
{
    (void)f;
    float c[3];
    theme_rgb(g_t * 0.01f, c);
    set3(p, "u_c0", c);
}

/* Each star has a fixed angle and a fixed place along the flow; where it
 * is now is that place plus the flow clock, wrapped round, so nothing per
 * star needs updating. Distance out is kept as the log of tan(phi / 2),
 * so stars spread and grow as they come out, the way they would flying
 * past. */
#define STARS_N   900
#define LP_NEAR  -4.0f                /* where stars are born (phi ~ 2 deg) */
#define LP_FAR    0.75f               /* past the rim */
#define STAR_TAIL 4                   /* dots in a streak at full speed */

static void stars_points(const DomeFrame *f)
{
    static const float TINT[4][3] = {
        { 1.0f, 1.0f, 1.0f }, { 0.80f, 0.88f, 1.0f }, { 1.0f, 0.92f, 0.80f }, { 0.90f, 0.85f, 1.0f }
    };
    float streak = 1.0f + 2.5f * L.react * g_gain * f->audio.bass;
    const float span = LP_FAR - LP_NEAR;
    pool_clear();
    for (int i = 0; i < STARS_N; i++) {
        float off = hash3i(i, 1, 0) * span;
        float pos = off + g_flow * 0.6f;
        float lap = floorf(pos / span);             /* a new angle every lap */
        float lp = LP_NEAR + (pos - lap * span);
        float theta = hash3i(i, (int)lap, 2) * 2.0f * DOME_PI;
        float bright = 0.4f + 0.6f * hash3i(i, 3, 0);
        const float *tint = TINT[i & 3];
        float ct = cosf(theta), st = sinf(theta);
        /* the streak: dots behind it, towards the middle, fading */
        int tail = (int)lroundf(fminf(1.0f, (streak - 1.0f) / 2.5f) * STAR_TAIL);
        for (int k = 0; k <= tail; k++) {
            float l = lp - 0.035f * (float)k * streak;
            float phi = 2.0f * atanf(expf(l));
            if (phi > 1.62f) continue;
            float a = bright * smooth01(0.03f, 0.35f, phi) * (1.0f - (float)k / (STAR_TAIL + 1));
            if (a < 0.02f) continue;
            float sp = sinf(phi);
            int bucket = phi < 0.55f ? 0 : phi < 1.05f ? 1 : 2;
            dome_points_push(&g_pool[bucket], sp * ct, sp * st, cosf(phi),
                             tint[0], tint[1], tint[2], a);
        }
    }
    pool_draw(f, NULL, 2.0f, 3.0f, 4.5f);
}

/* ---- 4: aurora, fixed to the dome: two curtains across the top half,
 * with fine rays. With music the folds sway on the beat's phase, further
 * when the snare body is loud; the overall brightness moves 15% at most.
 * u_k0 is the sway, u_c0 and u_c1 the low and high colours. ---- */
static const char AURORA[] =
    "float curtain(float c, float y, float base, float seed, float sway){\n"
    "  float b = base + (0.07 + 0.03 * abs(sway)) * sin(c * 2.0 + u_t * 0.15 + seed + sway * 0.5);\n"
    "  float h = y - b;\n"
    "  float rays = 0.55 + 0.45 * sin(c * 34.0 + 2.5 * sin(c * 4.0 + u_t * 0.3 + seed) + sway);\n"
    "  return smoothstep(0.0, 0.04, h) * exp(-max(h, 0.0) * 4.5) * rays;\n"
    "}\n"
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  float c = view.x * 1.7, y = view.y;\n"           /* across, and up */
    "  float a1 = curtain(c, y, 0.02, 0.0, u_k0);\n"
    "  float a2 = curtain(c * 1.3 + 0.7, y, 0.22, 3.0, -u_k0 * 0.7) * 0.55;\n"
    "  float top = clamp((y - 0.05) * 2.5, 0.0, 1.0) * (0.35 + 0.65 * u_air * u_react);\n"
    "  float lift = 0.85 + 0.15 * min(u_level * u_react, 1.0);\n"
    "  vec3 col = mix(u_c0, u_c1, top) * (a1 + a2) * 0.85 * lift * smoothstep(0.92, 0.6, y);\n"
    "  col += vec3(0.0, 0.008, 0.025) * (y + 1.0);\n"        /* night sky */
    "  return vec4(col * u_bright, 1.0);\n"
    "}\n";

static void aurora_uniforms(GLuint p, const DomeFrame *f)
{
    /* how far the folds have swung: slowly by themselves, in time with
     * the beat when music plays, further when the snare is loud */
    const Audio *a = &f->audio;
    float sw = sinf(g_t * 0.5f) * (1.0f - g_live) + sinf(2.0f * DOME_PI * a->phase) * g_live;
    set1(p, "u_k0", sw * (0.6f + 2.2f * a->body * g_gain * L.react));
    float lo[3], hi[3];
    theme_rgb(0.08f, lo);
    theme_rgb(0.32f, hi);
    set3(p, "u_c0", lo);
    set3(p, "u_c1", hi);
}

/* ---- a soft glow in the middle, the backdrop for styles drawn in dots:
 * u_c0 its colour, u_k0 its strength, u_k1 how tight it is ---- */
static const char GLOW[] =
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  float r = length(view.xy);\n"
    "  vec3 col = u_c0 * exp(-r * u_k1) * u_k0 * step(0.0, view.z);\n"
    "  return vec4(col * u_bright, 1.0);\n"
    "}\n";

static void glow_set(GLuint p, float x, float strength, float tight)
{
    float c[3];
    theme_rgb(x, c);
    set3(p, "u_c0", c);
    set1(p, "u_k0", strength);
    set1(p, "u_k1", tight);
}

static void glow_uniforms(GLuint p, const DomeFrame *f)
{
    (void)f;
    glow_set(p, g_t * 0.01f, 0.06f + 0.06f * g_live * L.react * f->audio.level, 6.0f);
}

/* a band's level for drawing: lifted like the spectrum ring's, 0..1 */
static float band_lift(const DomeFrame *f, int i)
{
    return sqrtf(fminf(1.0f, f->audio.band[i] * g_gain * L.react));
}

/* ---- 5: radial bars: 64 thin bars standing out from the middle,
 * mirrored left and right, bass at the bottom, each band twice. Bright
 * tips, which glow a little more on the beat. ---- */
static void radial_points(const DomeFrame *f)
{
    pool_clear();
    for (int i = 0; i < 64; i++) {
        float th = ((float)i + 0.5f) / 64.0f * 2.0f * DOME_PI;          /* 0 at the right */
        float rel = th + 0.5f * DOME_PI;                                  /* from the bottom */
        rel -= 2.0f * DOME_PI * floorf(rel / (2.0f * DOME_PI) + 0.5f);
        float b = fabsf(rel) / DOME_PI;                                   /* 0 bottom, 1 top */
        int band = (int)fminf(15.0f, b * 16.0f);
        float idle = 0.14f + 0.07f * sinf(b * 9.0f - g_t * 0.8f);
        float v = fmaxf(band_lift(f, band), idle * (1.0f - g_live));
        float len = 0.05f + 0.85f * v;                                    /* plane units, 1 = rim */
        float c[3];
        theme_rgb(b * 0.7f + g_t * 0.01f, c);
        float cx = cosf(th), sy = sinf(th);
        line_plane(SMALL, 0.04f * cx, 0.04f * sy, len * cx, len * sy, c, 0.85f);
        float w[3] = { 0.6f + 0.4f * c[0], 0.6f + 0.4f * c[1], 0.6f + 0.4f * c[2] };
        dot_plane(LARGE, len * cx, len * sy, w, 0.55f + 0.45f * g_swell * L.react);
    }
    pool_draw(f, NULL, 2.5f, 3.0f, 5.0f);
}

/* ---- 6: oscilloscope ring: a trace round the rim, its wave built from
 * the bands (the bass bends it in long waves, the treble adds fine
 * ripple; WLED sends no waveform, so the trace is made up from the real
 * levels), with two fading after-images. ---- */
#define SCOPE_N   720
#define SCOPE_OLD 2
static float g_scope[SCOPE_OLD + 1][SCOPE_N];    /* offsets: now, then older */
static float g_scope_acc, g_scope_ph[AUDIO_BANDS];

static void scope_update(const DomeFrame *f)
{
    /* each band drives its own harmonic round the ring: the bass long
     * slow waves, the treble fine fast ripple, each phase drifting */
    float amp[AUDIO_BANDS];
    for (int k = 0; k < AUDIO_BANDS; k++) {
        amp[k] = f->audio.band[k] * g_gain * L.react * (k < 3 ? 0.05f : 0.028f);
        g_scope_ph[k] += f->dt * L.speed * (0.6f + 0.35f * (float)k) * (k & 1 ? -1.0f : 1.0f);
    }
    /* after-images every 0.15 s */
    g_scope_acc += f->dt;
    if (g_scope_acc >= 0.15f) {
        g_scope_acc = 0.0f;
        memmove(g_scope[1], g_scope[0], SCOPE_OLD * sizeof g_scope[0]);
    }
    for (int i = 0; i < SCOPE_N; i++) {
        float th = (float)i / SCOPE_N * 2.0f * DOME_PI;
        float o = (1.0f - g_live) * 0.03f * sinf(3.0f * th + g_t * 0.6f);   /* idle: a slow sine */
        for (int k = 0; k < AUDIO_BANDS; k++) o += amp[k] * sinf((3.0f + 4.0f * k) * th + g_scope_ph[k]);
        g_scope[0][i] = o;
    }
}

static void scope_points(const DomeFrame *f)
{
    (void)f;
    pool_clear();
    float c[3], dim[3];
    theme_rgb(0.35f + g_t * 0.01f, c);
    for (int k = 0; k < 3; k++) dim[k] = c[k] * 0.5f;
    for (int h = SCOPE_OLD; h >= 0; h--) {
        float a = h == 0 ? 1.0f : 0.35f / (float)h;
        for (int i = 0; i < SCOPE_N; i++) {
            int j = (i + 1) % SCOPE_N;
            float t0 = (float)i / SCOPE_N * 2.0f * DOME_PI, t1 = (float)j / SCOPE_N * 2.0f * DOME_PI;
            float r0 = 0.72f + g_scope[h][i], r1 = 0.72f + g_scope[h][j];
            line_plane(h == 0 ? MID : SMALL, r0 * cosf(t0), r0 * sinf(t0),
                       r1 * cosf(t1), r1 * sinf(t1), h == 0 ? c : dim, a);
            if (h == 0)                                       /* the phosphor's glow */
                line_plane(LARGE, r0 * cosf(t0), r0 * sinf(t0), r1 * cosf(t1), r1 * sinf(t1), c, 0.12f);
        }
    }
    /* the baseline, faint, like a graticule */
    for (int i = 0; i < 120; i += 2) {
        float t = (float)i / 120.0f * 2.0f * DOME_PI;
        dot_plane(SMALL, 0.72f * cosf(t), 0.72f * sinf(t), dim, 0.25f);
    }
    pool_draw(f, NULL, 2.0f, 3.0f, 8.0f);
}

/* ---- 7: Lissajous: an a:b figure turning a quarter turn a beat (slowly
 * when idle), sized by the bass. Every 20 s it morphs to the next ratio
 * over 4 s rather than cutting. ---- */
static float g_liss_d;

static void liss_points(const DomeFrame *f)
{
    static const float RATIO[5][2] = { { 3, 2 }, { 5, 4 }, { 4, 3 }, { 5, 3 }, { 3, 1 } };
    const Audio *a = &f->audio;
    float bps = a->bpm > 0.0f ? a->bpm / 60.0f : 0.0f;
    g_liss_d += f->dt * L.speed * (0.2f * (1.0f - g_live) + g_live * bps * 0.5f * DOME_PI);
    float cyc = g_t / 20.0f;
    int k = (int)floorf(cyc);
    float e = smooth01(0.8f, 1.0f, cyc - (float)k);                 /* the last 4 s morph */
    const float *r0 = RATIO[k % 5], *r1 = RATIO[(k + 1) % 5];
    float fa = r0[0] + (r1[0] - r0[0]) * e, fb = r0[1] + (r1[1] - r0[1]) * e;
    float size = 0.5f + 0.28f * a->bass * g_gain * L.react + 0.04f * g_swell;
    float rot = g_t * 0.05f, cr = cosf(rot), sr = sinf(rot);
    pool_clear();
    const int N = 900;
    float px = 0, py = 0;
    for (int i = 0; i <= N; i++) {
        float s = (float)i / N * 2.0f * DOME_PI;
        float x = size * sinf(fa * s + g_liss_d), y = size * sinf(fb * s);
        float X = x * cr - y * sr, Y = x * sr + y * cr;
        if (i > 0) {
            float c[3];
            theme_rgb((float)i / N + g_t * 0.02f, c);
            line_plane(MID, px, py, X, Y, c, 0.9f);
            line_plane(LARGE, px, py, X, Y, c, 0.12f);           /* a soft glow round it */
        }
        px = X; py = Y;
    }
    pool_draw(f, NULL, 2.0f, 2.5f, 7.0f);
}

/* ---- 8: wire tunnel: rings flying out of the middle on the bass clock,
 * with spokes. Rings are spaced by the log of their distance, like the
 * starfield, so they speed up and spread as they come past; the bass
 * bends them and the beat's swell makes them bulge. ---- */
static void tunnel_points(const DomeFrame *f)
{
    const int RINGS = 14, SEG = 120, SPOKES = 12;
    const float span = 4.6f;
    float bend = 0.08f * f->audio.bass * g_gain * L.react, bulge = 0.10f * g_swell * L.react;
    pool_clear();
    for (int k = 0; k < RINGS; k++) {
        float pos = fmodf((float)k / RINGS * span + g_flow * 0.5f, span);
        float lp = -3.9f + pos;                              /* log of the plane radius */
        float r = expf(lp) * (1.0f + bulge);
        if (r > 1.08f) continue;
        float fade = smooth01(0.02f, 0.25f, r);
        float c[3];
        theme_rgb((float)k / RINGS + g_t * 0.02f, c);
        float px = 0, py = 0;
        for (int i = 0; i <= SEG; i++) {
            float th = (float)i / SEG * 2.0f * DOME_PI;
            float rr = r * (1.0f + bend * sinf(4.0f * th + g_t * 1.3f + (float)k));
            float x = rr * cosf(th), y = rr * sinf(th);
            if (i > 0) line_plane(r < 0.3f ? SMALL : MID, px, py, x, y, c, 0.9f * fade);
            px = x; py = y;
        }
    }
    float sc[3];
    theme_rgb(0.5f + g_t * 0.02f, sc);
    for (int s = 0; s < SPOKES; s++) {
        float th = ((float)s / SPOKES) * 2.0f * DOME_PI + g_t * 0.08f;
        for (int j = 0; j < 8; j++) {                         /* fading towards the middle */
            float r0 = 0.04f + 0.12f * j, r1 = r0 + 0.12f;
            line_plane(SMALL, r0 * cosf(th), r0 * sinf(th), r1 * cosf(th), r1 * sinf(th),
                       sc, 0.15f + 0.07f * j);
        }
    }
    pool_draw(f, NULL, 2.0f, 3.0f, 5.0f);
}

/* ---- 9: spectrum terrain: a wireframe landscape below a horizon across
 * the middle. Each row is a spectrum taken as it played, bass in the
 * middle and mirrored out; the rows scroll towards you. ---- */
#define TER_ROWS 24
#define TER_COLS 31                  /* bands mirrored: 15..0..15 */
static float g_ter[TER_ROWS][AUDIO_BANDS];   /* newest first */
static float g_ter_acc;

static void terrain_update(const DomeFrame *f)
{
    float step = 0.12f / L.speed;
    g_ter_acc += f->dt;
    while (g_ter_acc >= step) {
        g_ter_acc -= step;
        memmove(g_ter[1], g_ter[0], (TER_ROWS - 1) * sizeof g_ter[0]);
        for (int i = 0; i < AUDIO_BANDS; i++) {
            /* idle: low rolling hills */
            float hill = 0.25f + 0.2f * sinf((float)i * 0.7f + g_t * 0.9f);
            g_ter[0][i] = fmaxf(band_lift(f, i), hill * (1.0f - g_live));
        }
    }
}

/* a ground point in front of you (x across, z ahead, h up) as a direction */
static void ground_dir(float x, float z, float h, float out[3])
{
    float y = -2.0f + h * 1.6f;          /* the eye is 2 above the ground, above every peak */
    float l = sqrtf(x * x + y * y + z * z);
    out[0] = x / l; out[1] = y / l; out[2] = z / l;
}

static void terrain_points(const DomeFrame *f)
{
    const float ZFAR = 26.0f, ZNEAR = 0.5f;
    float frac = g_ter_acc / (0.12f / L.speed);       /* rows glide between steps */
    float pts[TER_ROWS][TER_COLS][3], hgt[TER_ROWS][TER_COLS], al[TER_ROWS];
    for (int r = 0; r < TER_ROWS; r++) {
        float t = ((float)r + frac) / (TER_ROWS - 1);    /* 0 newest (far) .. 1 near */
        float z = ZFAR * powf(ZNEAR / ZFAR, t);
        al[r] = smooth01(0.0f, 0.25f, t) * (1.0f - smooth01(0.96f, 1.0f, t));
        for (int c = 0; c < TER_COLS; c++) {
            int band = abs(c - TER_COLS / 2);
            float h = g_ter[r][band];
            hgt[r][c] = h;
            ground_dir((float)(c - TER_COLS / 2) * 1.0f, z, h, pts[r][c]);
        }
    }
    pool_clear();
    for (int r = 0; r < TER_ROWS; r++)
        for (int c = 0; c < TER_COLS; c++) {
            float col[3];
            theme_rgb(0.1f + hgt[r][c] * 0.45f + g_t * 0.01f, col);
            if (al[r] < 0.02f) continue;
            if (c + 1 < TER_COLS)
                dome_points_line(&g_pool[SMALL], pts[r][c], pts[r][c + 1], col[0], col[1], col[2], al[r], 0.007f);
            if (r + 1 < TER_ROWS && al[r + 1] > 0.02f)
                dome_points_line(&g_pool[SMALL], pts[r][c], pts[r + 1][c], col[0], col[1], col[2],
                                 0.6f * fminf(al[r], al[r + 1]), 0.007f);
        }
    pool_draw(f, NULL, 2.5f, 3.0f, 5.0f);
}

/* a dim horizon glow behind the terrain */
static const char HORIZON[] =
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  float h = exp(-abs(view.y) * 9.0) * step(0.0, view.z);\n"
    "  return vec4(u_c0 * h * (0.10 + 0.08 * u_level * u_react) * u_bright, 1.0);\n"
    "}\n";

static void horizon_uniforms(GLuint p, const DomeFrame *f)
{
    (void)f;
    float c[3];
    theme_rgb(0.3f + g_t * 0.01f, c);
    set3(p, "u_c0", c);
}

/* ================================================================== */
/* The table: one row per style, in N order                            */
/* ================================================================== */
typedef struct {
    const char *name;
    const char *glsl;                                   /* its surface() */
    void (*update)(const DomeFrame *f);                 /* its own state */
    void (*uniforms)(GLuint p, const DomeFrame *f);     /* its own uniforms */
    void (*points)(const DomeFrame *f);                 /* dots over it */
} Style;

static const Style STYLES[] = {
    { "spectrum ring", RING,    NULL,           NULL,             NULL },
    { "pulse globe",   PULSE,   NULL,           pulse_uniforms,   NULL },
    { "ripples",       RIPPLES, ripples_update, ripples_uniforms, NULL },
    { "starfield",     STARS,   NULL,           stars_uniforms,   stars_points },
    { "aurora",        AURORA,  NULL,           aurora_uniforms,  NULL },
    { "radial bars",   GLOW,    NULL,           glow_uniforms,    radial_points },
    { "oscilloscope",  GLOW,    scope_update,   glow_uniforms,    scope_points },
    { "lissajous",     GLOW,    NULL,           glow_uniforms,    liss_points },
    { "wire tunnel",   GLOW,    NULL,           glow_uniforms,    tunnel_points },
    { "terrain",       HORIZON, terrain_update, horizon_uniforms, terrain_points },
};
#define NSTYLE ((int)(sizeof STYLES / sizeof STYLES[0]))

static GLuint g_prog[NSTYLE];

static Param P[] = {
    { "style",  &L.style,  1.0f, 0.0f, NSTYLE - 1, 0,
      "0 spectrum ring, 1 pulse globe, 2 ripples, 3 starfield, 4 aurora, 5 radial bars, "
      "6 oscilloscope, 7 lissajous, 8 wire tunnel, 9 terrain (N)" },
    { "audio",  &L.audio,  1.0f, 0.0f, 1.0f, 0, "0 off (the idle look), 1 reacts to music (WLED audio sync)" },
    { "react",  &L.react,  0.1f, 0.0f, 2.0f, 0, "how strongly it reacts to the music" },
    { "speed",  &L.speed,  0.1f, 0.1f, 3.0f, 0, "speed of the motion" },
    { "theme",  &L.theme,  1.0f, 0.0f, 7.0f, 0,
      "0 rainbow, 1 fire, 2 ice, 3 neon, 4 aurora, 5 gold, 6 amber, 7 jewel (M)" },
    { "bright", &L.bright, 0.05f, 0.2f, 2.0f, 0, "brightness" },
};

static int style(void)
{
    int s = (int)lroundf(L.style);
    return s < 0 ? 0 : s >= NSTYLE ? NSTYLE - 1 : s;
}

static void init(void)
{
    size_t n = strlen(DOME_GLSL_THEMES) + sizeof MUSIC_LIB + 2;
    char *lib = malloc(n);
    snprintf(lib, n, "%s\n%s", DOME_GLSL_THEMES, MUSIC_LIB);
    for (int i = 0; i < NSTYLE; i++) g_prog[i] = dome_surface_program_lib(lib, STYLES[i].glsl);
    free(lib);
    themes_build();
    unsigned char zero[256] = { 0 };
    g_spec_tex = tex_new(AUDIO_BANDS, 1, GL_LUMINANCE, GL_CLAMP_TO_EDGE, zero);
    g_rip_tex = tex_new(RIP_N, 1, GL_LUMINANCE, GL_CLAMP_TO_EDGE, zero);
    g_noise_tex = noise_build();
    for (int b = 0; b < 3; b++) dome_points_init(&g_pool[b], POOL_N);
}

static void update(const DomeFrame *f)
{
    const Audio *a = &f->audio;
    float dt = f->dt;
    g_gain = ease_to(g_gain, L.audio > 0.5f ? 1.0f : 0.0f, 0.5f, dt);
    g_live = a->live * g_gain;
    float beat = a->beat * g_gain;
    g_swell = ease_to(g_swell, beat, beat > g_swell ? 0.06f : 0.15f, dt);
    g_beat_now = beat > g_prev_beat + 0.4f;
    g_prev_beat = beat;
    g_t += dt * L.speed;
    g_flow += dt * L.speed * (0.25f + 1.5f * L.react * g_gain * a->bass);
    const Style *st = &STYLES[style()];
    if (st->update) st->update(f);
}

static void draw(const DomeFrame *f)
{
    const Style *st = &STYLES[style()];
    GLuint p = g_prog[style()];
    dome_surface_begin(p, f);
    set1(p, "u_t", g_t);
    set1(p, "u_flow", g_flow);
    set1(p, "u_bright", L.bright);
    set1(p, "u_theme", floorf(L.theme + 0.5f));
    set1(p, "u_react", L.react);
    set1(p, "u_swell", g_swell);
    dome_audio_uniforms(p, f, g_gain);
    /* the shared textures: spectrum on unit 1, noise on 2, the style's own on 3 */
    float bands[AUDIO_BANDS];
    for (int i = 0; i < AUDIO_BANDS; i++) bands[i] = f->audio.band[i] * g_gain;
    glActiveTexture(GL_TEXTURE1);
    tex_row(g_spec_tex, bands, AUDIO_BANDS);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, g_noise_tex);
    glUniform1i(glGetUniformLocation(p, "u_spec"), 1);
    glUniform1i(glGetUniformLocation(p, "u_noise"), 2);
    glUniform1i(glGetUniformLocation(p, "u_tex"), 3);
    if (st->uniforms) st->uniforms(p, f);
    dome_surface_end();
    if (st->points) st->points(f);
}

static int key(SDL_Keycode k, int shift)
{
    (void)shift;
    if (k == SDLK_n) {
        L.style = (float)((style() + 1) % NSTYLE);
        printf("Music: %s\n", STYLES[style()].name);
        return 1;
    }
    if (k == SDLK_m) {
        L.theme = (float)(((int)lroundf(L.theme) + 1) % DOME_THEMES);
        printf("Music: %s colours\n", DOME_THEME_NAMES[(int)L.theme]);
        return 1;
    }
    return 0;
}

Scene scene_music = {
    "MUSIC", "music.cfg", 0, P, (int)(sizeof P / sizeof P[0]),
    init, NULL, update, draw, key,
    "N next style, M next colours", NULL
};
