/*
 * music.c  -  Music: built for sound, from WLED audio sync (dome/audio.h).
 * N steps through the styles, M through the colour themes:
 *
 *   0 spectrum ring   16 bars standing in from the rim, bass at the bottom,
 *                     treble at the top, mirrored left and right
 *   1 pulse globe     a glowing ball in the middle that swells with the
 *                     bass and the beat, with a halo from the treble
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

#define NSTYLE 2
static const char *NAMES[NSTYLE] = { "spectrum ring", "pulse globe" };

static struct { float style, audio, react, speed, theme, bright; } L =
    { 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f };

static Param P[] = {
    { "style",  &L.style,  1.0f, 0.0f, NSTYLE - 1, 0,
      "0 spectrum ring, 1 pulse globe (N)" },
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
 * the bass. u_tex is the spectrum (16x1). */
#define U "uniform float u_t, u_flow, u_bright, u_theme, u_react, u_swell;\n" \
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

static const char *GLSL[NSTYLE] = { RING, PULSE };

static GLuint g_prog[NSTYLE];
static GLuint g_spec_tex;            /* 16x1: the bands, 0..255 */
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
    unsigned char zero[256] = { 0 };
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
