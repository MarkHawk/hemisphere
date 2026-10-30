/*
 * music.c  -  Music: built for sound, from WLED audio sync (dome/audio.h).
 * N steps through the styles, M through the colour themes:
 *
 *   0 spectrum ring   16 bars standing in from the rim, bass at the bottom,
 *                     treble at the top, mirrored left and right
 *   1 pulse globe     a glowing ball in the middle that swells with the
 *                     bass and the beat, with a halo from the treble
 *   2 ripples         every snare hit sends a ring out from the middle
 *   3 starfield       stars streaming out from the middle, faster with
 *                     the bass, stretching into streaks at speed
 *   4 aurora          curtains of light across the top of the dome,
 *                     swaying in time and further with the snare body,
 *                     their tops lifting in colour with the treble
 *   5 planet          a slowly turning world whose city lights and
 *                     atmosphere breathe with the music
 *
 * With no music every style has a calm, slow idle of its own, and the
 * change between idle and music is eased over about a second.
 *
 * NO FLASHING: a beat never lights the whole dome. Beats swell sizes, move
 * things and light small areas, always eased (u_swell, never the raw
 * beat), and sound changes the overall brightness by 15% at most.
 */
#include "../dome/dome.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define NSTYLE 6
static const char *NAMES[NSTYLE] = {
    "spectrum ring", "pulse globe", "ripples", "starfield", "aurora", "planet"
};

static struct { float style, audio, react, speed, theme, bright; } L =
    { 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f };

static Param P[] = {
    { "style",  &L.style,  1.0f, 0.0f, NSTYLE - 1, 0,
      "0 spectrum ring, 1 pulse globe, 2 ripples, 3 starfield, 4 aurora, 5 planet (N)" },
    { "audio",  &L.audio,  1.0f, 0.0f, 1.0f, 0, "0 off (the idle look), 1 reacts to music (WLED audio sync)" },
    { "react",  &L.react,  0.1f, 0.0f, 2.0f, 0, "how strongly it reacts to the music" },
    { "speed",  &L.speed,  0.1f, 0.1f, 3.0f, 0, "speed of the motion" },
    { "theme",  &L.theme,  1.0f, 0.0f, 7.0f, 0,
      "0 rainbow, 1 fire, 2 ice, 3 neon, 4 aurora, 5 gold, 6 amber, 7 jewel (M)" },
    { "bright", &L.bright, 0.05f, 0.2f, 2.0f, 0, "brightness" },
};

/* Every style gets these. u_bass..u_live come from dome_audio_uniforms,
 * already faded by the scene's gain; u_swell is the beat with its leap
 * eased; u_t is the scene's clock, u_flow a clock that runs faster with
 * the bass. u_tex is the spectrum (16x1) or the ripples (256x1); u_streak
 * is how far stars stretch. */
#define U "uniform float u_t, u_flow, u_bright, u_theme, u_react, u_swell, u_streak;\n" \
          "uniform float u_bass, u_body, u_mid, u_air, u_level, u_phase, u_live;\n" \
          "uniform sampler2D u_tex;\n"

/* ---- 0: spectrum ring ---- */
static const char RING[] =
    U
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  float phi = acos(clamp(view.z, -1.0, 1.0));\n"     /* 0 centre, ~1.57 rim */
    /* round the dome: 0 at the bottom, 1 at the top, the same both sides */
    "  float b = abs(atan(view.x, -view.y)) / PI;\n"
    "  float x = b * 16.0, i = floor(x), fx = fract(x);\n"
    "  float v = texture2D(u_tex, vec2((i + 0.5) / 16.0, 0.5)).r;\n"
    /* with no music, a slow faint wave runs round the rim */
    "  float idle = 0.10 + 0.06 * sin(b * 9.0 - u_t * 0.8);\n"
    "  v = max(v * u_react, idle * (1.0 - u_live));\n"
    "  float h = 0.06 + 0.62 * min(v, 1.2);\n"            /* bar height, radians */
    "  float in_bar = smoothstep(0.08, 0.16, fx) * smoothstep(0.92, 0.84, fx);\n"
    "  float d = phi - (PI * 0.5 - h);\n"                 /* > 0 inside the bar */
    "  float bar = smoothstep(-0.01, 0.01, d) * in_bar;\n"
    /* bright tip, and a soft glow above it that swells on the beat */
    "  float tip = exp(-d * d * 900.0) * in_bar;\n"
    "  float glow = exp(-max(-d, 0.0) * 9.0) * (0.10 + 0.25 * u_swell * u_react) * in_bar;\n"
    "  vec3 c = theme(u_theme, b * 0.7 + u_t * 0.01);\n"
    "  vec3 col = c * (bar * (0.35 + 0.65 * d / h) + glow) + vec3(1.0) * tip * 0.35 * bar;\n"
    "  return vec4(col * u_bright, 1.0);\n"
    "}\n";

/* ---- 1: pulse globe ---- */
static const char PULSE[] =
    U
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  float phi = acos(clamp(view.z, -1.0, 1.0));\n"
    /* the ball's size: bass, the beat's swell, and a slow breath when idle */
    "  float R = 0.38 + u_react * (0.22 * u_bass + 0.12 * u_swell)\n"
    "          + (1.0 - u_live) * 0.05 * sin(u_t * 0.63);\n"   /* 6 breaths a minute */
    "  float k = phi / R;\n"
    "  float ball = sqrt(max(0.0, 1.0 - k * k));\n"        /* round shading */
    "  float edge = smoothstep(1.0, 0.97, k);\n"
    "  float halo = exp(-max(phi - R, 0.0) * 7.0) * (1.0 - edge)\n"
    "             * (0.18 + 0.30 * u_air * u_react);\n"
    /* colour drifts slowly round the theme, the core paler */
    "  vec3 c = theme(u_theme, u_t * 0.02 + k * 0.15);\n"
    "  vec3 col = c * (0.25 + 0.75 * ball) * edge + mix(c, vec3(1.0), 0.6) * pow(ball, 6.0) * 0.35 * edge\n"
    "           + c * halo;\n"
    /* faint rings in the background, turning slowly */
    "  float bg = 0.04 * (0.5 + 0.5 * sin(phi * 14.0 - u_t * 0.5));\n"
    "  col += theme(u_theme, 0.5 + u_t * 0.01) * bg * (1.0 - edge);\n"
    "  return vec4(col * u_bright, 1.0);\n"
    "}\n";

/* ---- 2: ripples. The CPU keeps the last few seconds of beats in a
 * 256x1 texture, newest at the left, shifted along as time passes; read
 * at the angle from the middle, each beat is a ring moving out. ---- */
static const char RIPPLES[] =
    U
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  float phi = acos(clamp(view.z, -1.0, 1.0));\n"
    "  float u = phi / (PI * 0.5);\n"
    "  float r = texture2D(u_tex, vec2(0.01 + 0.98 * u, 0.5)).r * (1.0 - 0.55 * u);\n"
    "  vec3 c = theme(u_theme, u * 0.4 + u_t * 0.015);\n"
    "  vec3 col = c * r * 1.2 + vec3(1.0) * r * r * r * 0.35;\n"
    /* a soft standing glow in the middle from the overall level */
    "  col += c * exp(-phi * 4.0) * (0.06 + 0.18 * u_level * u_react);\n"
    /* and very faint fixed rings, so the dome is never quite empty */
    "  col += theme(u_theme, 0.5) * 0.025 * (0.5 + 0.5 * cos(phi * 20.0));\n"
    "  return vec4(col * u_bright, 1.0);\n"
    "}\n";

/* ---- 3: starfield. Cells laid out by angle round the middle and by the
 * log of the distance from it, so stars grow as they come out; three
 * layers, written out. u_flow moves them, u_streak stretches them. ---- */
static const char STARS[] =
    U
    "float layer(float theta, float lp, float n, float seed){\n"
    "  float k = n / (2.0 * PI);\n"                      /* square cells */
    "  vec2 g = vec2(theta / (2.0 * PI) * n, (lp - u_flow * 0.6) * k);\n"
    "  vec2 id = floor(g), fr = fract(g) - 0.5;\n"
    "  float h = h3(vec3(id, seed));\n"
    "  vec2 o = vec2(h3(vec3(id, seed + 1.0)), h3(vec3(id, seed + 2.0))) - 0.5;\n"
    "  vec2 d = fr - o * 0.6;\n"
    "  d.y /= u_streak;\n"                               /* stretch outwards */
    "  return exp(-dot(d, d) * 160.0) * step(0.55, h) * (0.4 + 0.6 * fract(h * 7.0));\n"
    "}\n"
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  float phi = acos(clamp(view.z, -1.0, 1.0));\n"
    "  float theta = atan(view.y, view.x), lp = log(max(phi, 0.005));\n"
    "  float s = layer(theta, lp, 48.0, 1.0) + layer(theta, lp, 80.0, 7.0) * 0.8\n"
    "          + layer(theta, lp, 130.0, 13.0) * 0.6;\n"
    "  s *= smoothstep(0.02, 0.3, phi);\n"               /* born in the distance */
    "  vec3 c = mix(vec3(1.0), theme(u_theme, theta / (2.0 * PI) + u_t * 0.01), 0.35);\n"
    /* a faint glow at the vanishing point */
    "  vec3 col = c * s + theme(u_theme, u_t * 0.01) * exp(-phi * 6.0) * 0.08;\n"
    "  return vec4(col * u_bright, 1.0);\n"
    "}\n";

/* ---- 4: aurora, fixed to the dome: two curtains across the top half,
 * with fine rays. With music the folds sway on the beat's phase, further
 * when the snare body is loud; the overall brightness moves 15% at most. */
static const char AURORA[] =
    U
    "float curtain(float c, float y, float base, float seed, float sway){\n"
    "  float b = base + 0.07 * sin(c * 2.0 + u_t * 0.15 + seed)\n"
    "        + sway * 0.06 * sin(c * 3.5 + seed * 2.0);\n"
    "  float h = y - b;\n"
    "  float rays = 0.55 + 0.45 * sin(c * 34.0 + 2.5 * sin(c * 4.0 + u_t * 0.3 + seed) + sway);\n"
    "  return smoothstep(0.0, 0.04, h) * exp(-max(h, 0.0) * 4.5) * rays;\n"
    "}\n"
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  float c = atan(view.x, view.z), y = view.y;\n"
    "  float sway = mix(sin(u_t * 0.5), sin(6.2832 * u_phase), u_live)\n"
    "             * (0.6 + 2.2 * u_body * u_react);\n"
    "  float a1 = curtain(c, y, 0.02, 0.0, sway);\n"
    "  float a2 = curtain(c * 1.3 + 0.7, y, 0.22, 3.0, -sway * 0.7) * 0.55;\n"
    "  float top = clamp((y - 0.05) * 2.5, 0.0, 1.0) * (0.35 + 0.65 * u_air * u_react);\n"
    "  vec3 low = theme(u_theme, 0.08 + 0.05 * sin(c + u_t * 0.05));\n"
    "  vec3 high = theme(u_theme, 0.32);\n"
    "  float lift = 0.85 + 0.15 * min(u_level * u_react, 1.0);\n"
    "  vec3 col = mix(low, high, top) * (a1 + a2) * 0.85 * lift * smoothstep(0.92, 0.6, y);\n"
    "  col += vec3(0.0, 0.008, 0.025) * (y + 1.0);\n"        /* night sky */
    "  return vec4(col * u_bright, 1.0);\n"
    "}\n";

/* ---- 5: planet: turns with the globe. Lit from the upper left; on the
 * night side its cities glow, and they and the atmosphere breathe with
 * the overall level. One cheap noise for the land, no fbm. */
static const char PLANET[] =
    U
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  float n = n3(dir * 2.2) * 0.7 + n3(dir * 5.5) * 0.3;\n"
    "  float land = smoothstep(0.50, 0.53, n);\n"
    "  float ice = smoothstep(0.80, 0.86, abs(dir.y));\n"
    "  vec3 sea = vec3(0.02, 0.06, 0.16), ground = mix(vec3(0.10, 0.22, 0.08), vec3(0.30, 0.26, 0.16), smoothstep(0.55, 0.7, n));\n"
    "  vec3 surf = mix(mix(sea, ground, land), vec3(0.45, 0.5, 0.55), ice);\n"
    "  vec3 L = normalize(vec3(-0.7, 0.35, 0.6));\n"
    "  float day = smoothstep(-0.12, 0.3, dot(view, L));\n"
    "  vec3 col = surf * (0.08 + 0.92 * day) * max(dot(view, L), 0.0);\n"
    /* cities: dots in some cells of a grid on the land, on the night side */
    "  vec3 g = dir * 34.0;\n"
    "  float h = h3(floor(g));\n"
    "  float city = smoothstep(0.30, 0.05, length(fract(g) - 0.5)) * step(0.72, h) * land * (1.0 - ice);\n"
    "  float breathe = mix(0.75, 0.35 + 0.65 * min(u_level * u_react, 1.0), u_live);\n"
    "  col += theme(u_theme, 0.15 + h * 0.2) * city * (1.0 - day) * breathe * 1.4;\n"
    /* atmosphere round the edge, brighter on the day side */
    "  float rim = pow(1.0 - max(view.z, 0.0), 3.0);\n"
    "  float air = 0.30 + 0.20 * min(u_level * u_react, 1.0) + 0.12 * u_swell * u_react;\n"
    "  col += mix(vec3(0.3, 0.55, 1.0), theme(u_theme, 0.05), 0.3) * rim * air * (0.35 + 0.65 * day);\n"
    "  return vec4(col * u_bright, 1.0);\n"
    "}\n";

static const char *GLSL[NSTYLE] = { RING, PULSE, RIPPLES, STARS, AURORA, PLANET };

static GLuint g_prog[NSTYLE];
static GLuint g_spec_tex;            /* 16x1: the bands, 0..255 */
#define RIP_N    256                 /* texels in the ripple history */
#define RIP_SECS 3.0f                /* seconds for a ring to reach the rim */
static GLuint g_rip_tex;
static float  g_rip[RIP_N];          /* newest first */
static float  g_rip_acc;
static float  g_t, g_flow;           /* the scene's clocks */
static float  g_gain;                /* eases to the audio setting */
static float  g_swell;               /* the beat, with its leap eased */

static int style(void)
{
    int s = (int)lroundf(L.style);
    return s < 0 ? 0 : s >= NSTYLE ? NSTYLE - 1 : s;
}

static GLuint tex_1d(int w)
{
    GLuint t;
    unsigned char zero[RIP_N] = { 0 };
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, w, 1, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, zero);
    return t;
}

static void init(void)
{
    for (int i = 0; i < NSTYLE; i++) g_prog[i] = dome_surface_program_themed(GLSL[i]);
    g_spec_tex = tex_1d(AUDIO_BANDS);
    g_rip_tex = tex_1d(RIP_N);
}

static float ease_to(float v, float target, float tau, float dt)
{
    return v + (target - v) * (1.0f - expf(-dt / tau));
}

static void update(const DomeFrame *f)
{
    const Audio *a = &f->audio;
    float dt = f->dt;
    g_gain = ease_to(g_gain, L.audio > 0.5f ? 1.0f : 0.0f, 0.5f, dt);
    float beat = a->beat * g_gain;
    g_swell = ease_to(g_swell, beat, beat > g_swell ? 0.06f : 0.15f, dt);
    g_t += dt * L.speed;
    g_flow += dt * L.speed * (0.25f + 1.5f * L.react * g_gain * a->bass);

    if (style() == 2) {
        /* what enters the middle now: the eased beat, weighted by the
         * snare body; with no music, a soft ring every 4.5 seconds */
        float idle = fmodf(g_t, 4.5f) - 0.2f;
        float in = g_swell * L.react * (0.55f + 0.45f * a->body * g_gain)
                 + (1.0f - a->live * g_gain) * 0.45f * expf(-idle * idle / 0.012f);
        float step = RIP_SECS / (RIP_N * L.speed);
        for (g_rip_acc += dt; g_rip_acc >= step; g_rip_acc -= step) {
            memmove(g_rip + 1, g_rip, (RIP_N - 1) * sizeof g_rip[0]);
            g_rip[0] = fminf(1.0f, in);
        }
    }
}

static void draw(const DomeFrame *f)
{
    int s = style();
    GLuint p = g_prog[s];
    dome_surface_begin(p, f);
    glUniform1f(glGetUniformLocation(p, "u_t"), g_t);
    glUniform1f(glGetUniformLocation(p, "u_flow"), g_flow);
    glUniform1f(glGetUniformLocation(p, "u_bright"), L.bright);
    glUniform1f(glGetUniformLocation(p, "u_theme"), floorf(L.theme + 0.5f));
    glUniform1f(glGetUniformLocation(p, "u_react"), L.react);
    glUniform1f(glGetUniformLocation(p, "u_swell"), g_swell);
    dome_audio_uniforms(p, f, g_gain);
    if (s == 0) {
        unsigned char px[AUDIO_BANDS];
        for (int i = 0; i < AUDIO_BANDS; i++)
            px[i] = (unsigned char)lroundf(fminf(1.0f, f->audio.band[i] * g_gain) * 255.0f);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, g_spec_tex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, AUDIO_BANDS, 1, GL_LUMINANCE, GL_UNSIGNED_BYTE, px);
        glUniform1i(glGetUniformLocation(p, "u_tex"), 1);
    }
    if (s == 2) {
        unsigned char px[RIP_N];
        for (int i = 0; i < RIP_N; i++) px[i] = (unsigned char)lroundf(g_rip[i] * 255.0f);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, g_rip_tex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, RIP_N, 1, GL_LUMINANCE, GL_UNSIGNED_BYTE, px);
        glUniform1i(glGetUniformLocation(p, "u_tex"), 1);
    }
    if (s == 3)
        glUniform1f(glGetUniformLocation(p, "u_streak"),
                    1.0f + 2.5f * L.react * g_gain * f->audio.bass);
    dome_surface_end();
}

static int key(SDL_Keycode k, int shift)
{
    (void)shift;
    if (k == SDLK_n) {
        L.style = (float)((style() + 1) % NSTYLE);
        printf("Music: %s\n", NAMES[style()]);
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
