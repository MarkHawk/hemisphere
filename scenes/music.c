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
static DomePoints g_star_pts[3];      /* small, middle, large dots */

static void stars_points(const DomeFrame *f)
{
    static const float TINT[4][3] = {
        { 1.0f, 1.0f, 1.0f }, { 0.80f, 0.88f, 1.0f }, { 1.0f, 0.92f, 0.80f }, { 0.90f, 0.85f, 1.0f }
    };
    float streak = 1.0f + 2.5f * L.react * g_gain * f->audio.bass;
    const float span = LP_FAR - LP_NEAR;
    for (int b = 0; b < 3; b++) dome_points_clear(&g_star_pts[b]);
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
            dome_points_push(&g_star_pts[bucket], sp * ct, sp * st, cosf(phi),
                             tint[0], tint[1], tint[2], a);
        }
    }
    mat3 id;
    m3_identity(id);                     /* stars are fixed to the dome */
    static const float SIZE[3] = { 2.0f, 3.0f, 4.5f };
    for (int b = 0; b < 3; b++) {
        dome_points_upload(&g_star_pts[b]);
        dome_draw_points(&g_star_pts[b], id, f->fade * fminf(1.0f, L.bright), SIZE[b]);
    }
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
};
#define NSTYLE ((int)(sizeof STYLES / sizeof STYLES[0]))

static GLuint g_prog[NSTYLE];

static Param P[] = {
    { "style",  &L.style,  1.0f, 0.0f, NSTYLE - 1, 0,
      "0 spectrum ring, 1 pulse globe, 2 ripples, 3 starfield, 4 aurora (N)" },
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
    for (int b = 0; b < 3; b++) dome_points_init(&g_star_pts[b], STARS_N * (STAR_TAIL + 1));
}

static void update(const DomeFrame *f)
{
    const Audio *a = &f->audio;
    float dt = f->dt;
    g_gain = ease_to(g_gain, L.audio > 0.5f ? 1.0f : 0.0f, 0.5f, dt);
    g_live = a->live * g_gain;
    float beat = a->beat * g_gain;
    g_swell = ease_to(g_swell, beat, beat > g_swell ? 0.06f : 0.15f, dt);
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
