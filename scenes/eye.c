/*
 * eye.c  -  a giant eye that fills the dome and looks around.
 *
 * The eye does not spin with the other scenes: it is drawn in dome space,
 * facing out of the front of the globe. Every so often it glances
 * somewhere new (a quick flick, then a hold, like real eyes), with a
 * faint tremor, and now and then it looks straight back at you.
 *
 *   style 0  human eye: iris fibres, veined white, pupil that breathes,
 *            and eyelids that blink
 *   style 1  the Eye of Sauron: a slit pupil in a churning eye of fire
 */
#include "../dome/dome.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static struct { float style, iris, pupil, look, busy, lids, blink, bright; } L =
    { 0.0f, 0.15f, 0.36f, 18.0f, 1.0f, 1.0f, 8.0f, 1.0f };

static Param P[] = {
    { "style",  &L.style,  1.0f,  0.0f, 1.0f, 0, "0 = human eye, 1 = Eye of Sauron" },
    { "iris",   &L.iris,   0.05f, 0.0f, 1.0f, 0,
      "human iris colour: 0 blue, 0.5 green, 0.8 hazel, 1 brown" },
    { "pupil",  &L.pupil,  0.02f, 0.10f, 0.80f, 0, "pupil size (Sauron: slit width)" },
    { "look",   &L.look,   1.0f,  0.0f, 40.0f, 0, "how far it looks around, degrees" },
    { "busy",   &L.busy,   0.1f,  0.1f, 4.0f, 0, "how often it glances somewhere new" },
    { "lids",   &L.lids,   1.0f,  0.0f, 1.0f, 0, "human eyelids: 0 = bare eyeball, 1 = lids" },
    { "blink",  &L.blink,  1.0f,  0.0f, 40.0f, 0, "blinks per minute (0 = never)" },
    { "bright", &L.bright, 0.05f, 0.2f, 2.0f, 0, "brightness" },
};

static const char *EYE_GLSL =
    "uniform mat3  u_gaze;\n"                 /* dome -> eye coordinates */
    "uniform float u_style, u_iris, u_pupil, u_lid, u_lids, u_bright, u_t;\n"
    "float h3(vec3 p){ return fract(sin(dot(p, vec3(127.1, 311.7, 74.7))) * 43758.5453); }\n"
    "float n3(vec3 p){\n"
    "  vec3 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);\n"
    "  return mix(mix(mix(h3(i), h3(i + vec3(1,0,0)), f.x),\n"
    "                 mix(h3(i + vec3(0,1,0)), h3(i + vec3(1,1,0)), f.x), f.y),\n"
    "             mix(mix(h3(i + vec3(0,0,1)), h3(i + vec3(1,0,1)), f.x),\n"
    "                 mix(h3(i + vec3(0,1,1)), h3(i + vec3(1,1,1)), f.x), f.y), f.z);\n"
    "}\n"
    "vec3 fire(float x){\n"                    /* black, red, orange, yellow, white */
    "  x = clamp(x, 0.0, 1.0);\n"
    "  return vec3(smoothstep(0.0, 0.35, x), smoothstep(0.25, 0.75, x) * 0.85,\n"
    "              smoothstep(0.7, 1.0, x) * 0.7);\n"
    "}\n"
    /* ---- human ---- */
    "vec3 human(vec3 p, float a, float th, vec3 view){\n"
    "  float IR = 0.62, PR = IR * u_pupil;\n"
    "  vec3 ring = vec3(cos(th), sin(th), 0.0);\n"
    "  vec3 col;\n"
    /* iris colour: blue -> green -> hazel -> brown */
    "  vec3 base = u_iris < 0.5 ? mix(vec3(0.25, 0.48, 0.80), vec3(0.32, 0.55, 0.30), u_iris * 2.0)\n"
    "            : mix(vec3(0.32, 0.55, 0.30), vec3(0.42, 0.24, 0.10), (u_iris - 0.5) * 2.0);\n"
    "  if (a < IR) {\n"
    "    float r = a / IR;\n"
    "    float fib = n3(ring * 9.0 + vec3(0.0, 0.0, r * 3.0))\n"
    "              * 0.6 + n3(ring * 26.0 + vec3(0.0, 0.0, r * 7.0)) * 0.4;\n"
    "    col = base * (0.55 + 0.9 * fib);\n"
    /* warm collarette round the pupil, dark ring at the edge of the iris */
    "    col = mix(col, vec3(0.75, 0.55, 0.25), smoothstep(0.55, 0.35, r) * 0.45);\n"
    "    col *= mix(1.0, 0.25, smoothstep(0.80, 1.0, r));\n"
    /* pupil */
    "    col *= smoothstep(PR - 0.01, PR + 0.02, a);\n"
    "  } else {\n"
    /* the white, with fine red veins thickening towards the back */
    "    float v = abs(n3(p * 6.0) - 0.5) + abs(n3(p * 13.0 + 5.0) - 0.5) * 0.5;\n"
    "    float vein = smoothstep(0.035, 0.0, v) * smoothstep(0.7, 1.6, a);\n"
    "    col = vec3(0.93, 0.90, 0.86) * (0.92 + 0.08 * n3(p * 20.0));\n"
    "    col = mix(col, vec3(0.75, 0.15, 0.12), vein * 0.8);\n"
    "    col *= mix(1.0, 0.55, smoothstep(0.9, 1.9, a));\n"
    /* soft edge where the iris meets the white */
    "    col = mix(base * 0.2, col, smoothstep(IR, IR + 0.04, a));\n"
    "  }\n"
    /* the cornea's highlight stays put while the eye moves */
    "  float spec = pow(max(dot(view, normalize(vec3(-0.35, 0.45, 0.82))), 0.0), 220.0);\n"
    "  col += vec3(1.0) * spec * 0.9 * step(a, IR + 0.2);\n"
    /* eyelids, fixed to the dome: almond opening that closes on a blink */
    "  if (u_lids > 0.5) {\n"
    "    float x = view.x;\n"
    "    float top = mix(0.93, -0.08, u_lid) - 0.30 * x * x;\n"
    "    float bot = mix(-0.86, -0.10, u_lid) + 0.25 * x * x;\n"
    "    float edge = min(top - view.y, view.y - bot);\n"
    "    vec3 skin = vec3(0.78, 0.56, 0.46) * (0.75 + 0.25 * n3(view * 18.0));\n"
    "    skin *= 0.55 + 0.45 * clamp(view.z + 0.3, 0.0, 1.0);\n"
    "    col *= smoothstep(0.0, 0.10, edge) * 0.6 + 0.4 * step(0.0, edge);\n"   /* lid shadow */
    "    col = mix(skin, col, smoothstep(-0.005, 0.005, edge));\n"
    "    col = mix(col, vec3(0.08, 0.05, 0.04), smoothstep(0.03, 0.0, abs(edge)) * 0.85);\n" /* lashes */
    "  }\n"
    "  return col;\n"
    "}\n"
    /* ---- Sauron ---- */
    "vec3 sauron(vec3 p, float a, float th){\n"
    "  float ex = a * cos(th), ey = a * sin(th);\n"
    /* flames rushing outwards from the pupil */
    "  vec3 q = p * 3.0 - vec3(0.0, 0.0, u_t * 0.9) * (1.0 + a);\n"
    "  float fl = n3(q) * 0.6 + n3(q * 2.3 + 7.0) * 0.4;\n"
    "  vec3 ring = vec3(cos(th), sin(th), 0.0);\n"
    "  float streak = n3(ring * 14.0 + vec3(0.0, 0.0, a * 4.0 - u_t * 1.5));\n"
    "  float I = exp(-a * 1.1) * 1.25 + (fl - 0.5) * 0.9 + (streak - 0.5) * 0.5 * exp(-a * 2.0);\n"
    "  vec3 col = fire(I);\n"
    /* the slit pupil, ringed with white heat */
    "  float w = 0.10 * u_pupil / 0.36, h = 0.62;\n"
    "  float s = (ex * ex) / (w * w) + (ey * ey) / (h * h);\n"
    "  col = mix(col, vec3(1.0, 0.95, 0.75), smoothstep(2.6, 1.2, s) * 0.8);\n"
    "  col *= smoothstep(0.75, 1.05, s);\n"
    "  return col;\n"
    "}\n"
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  vec3 p = u_gaze * view;\n"
    "  float a = acos(clamp(p.z, -1.0, 1.0));\n"   /* angle from the eye's axis */
    "  float th = atan(p.y, p.x);\n"
    "  vec3 col = u_style < 0.5 ? human(p, a, th, view) : sauron(p, a, th);\n"
    "  return vec4(col * u_bright, 1.0);\n"
    "}\n";

static GLuint g_prog;
static float  g_t;
static float  g_yaw, g_pitch, g_tyaw, g_tpitch, g_hold = 1.0f;
static float  g_blink_in = 3.0f, g_blink_t = -1.0f;

static float frnd(void) { return (float)rand() / (float)RAND_MAX; }

static void init(void) { g_prog = dome_surface_program(EYE_GLSL); }

static void update(const DomeFrame *f)
{
    float dt = f->dt > 0.1f ? 0.1f : f->dt;
    g_t += dt;

    /* glances: hold, then flick to a new target; sometimes back at you */
    g_hold -= dt * L.busy;
    if (g_hold <= 0.0f) {
        float r = DEG2RAD(L.look);
        if (frnd() < 0.25f) { g_tyaw = 0.0f; g_tpitch = 0.0f; }
        else { g_tyaw = (frnd() * 2.0f - 1.0f) * r; g_tpitch = (frnd() * 2.0f - 1.0f) * r * 0.6f; }
        g_hold = 0.8f + frnd() * 2.8f;
    }
    float k = 1.0f - expf(-dt * 14.0f);           /* fast, like a saccade */
    g_yaw += (g_tyaw - g_yaw) * k;
    g_pitch += (g_tpitch - g_pitch) * k;

    /* blinks */
    if (L.blink > 0.0f) {
        g_blink_in -= dt;
        if (g_blink_in <= 0.0f && g_blink_t < 0.0f) {
            g_blink_t = 0.0f;
            g_blink_in = (60.0f / L.blink) * (0.5f + frnd());
        }
    }
    if (g_blink_t >= 0.0f) {
        g_blink_t += dt;
        if (g_blink_t > 0.28f) g_blink_t = -1.0f;
    }
}

static void draw(const DomeFrame *f)
{
    /* small tremor on top of the glance */
    float yaw = g_yaw + 0.004f * sinf(g_t * 23.0f) + 0.003f * sinf(g_t * 37.0f);
    float pitch = g_pitch + 0.004f * sinf(g_t * 29.0f + 1.0f);
    mat3 ry, rx, r, gaze;
    m3_ry(ry, yaw);
    m3_rx(rx, -pitch);
    m3_mul(r, ry, rx);
    m3_transpose(gaze, r);

    /* lid closes fast, opens a little slower */
    float lid = 0.0f;
    if (g_blink_t >= 0.0f)
        lid = g_blink_t < 0.10f ? g_blink_t / 0.10f : 1.0f - (g_blink_t - 0.10f) / 0.18f;
    if (lid < 0.0f) lid = 0.0f;

    /* the pupil breathes a little */
    float pupil = L.pupil * (1.0f + 0.06f * sinf(g_t * 0.7f));

    GLuint p = g_prog;
    dome_surface_begin(p, f);
    glUniformMatrix3fv(glGetUniformLocation(p, "u_gaze"), 1, GL_FALSE, gaze);
    glUniform1f(glGetUniformLocation(p, "u_style"), L.style);
    glUniform1f(glGetUniformLocation(p, "u_iris"), L.iris);
    glUniform1f(glGetUniformLocation(p, "u_pupil"), pupil);
    glUniform1f(glGetUniformLocation(p, "u_lid"), L.style < 0.5f ? lid : 0.0f);
    glUniform1f(glGetUniformLocation(p, "u_lids"), L.style < 0.5f ? L.lids : 0.0f);
    glUniform1f(glGetUniformLocation(p, "u_bright"), L.bright);
    glUniform1f(glGetUniformLocation(p, "u_t"), g_t);
    dome_surface_end();
}

static int key(SDL_Keycode k, int shift)
{
    (void)shift;
    if (k != SDLK_n) return 0;
    L.style = L.style < 0.5f ? 1.0f : 0.0f;
    printf("Eye: %s\n", L.style < 0.5f ? "human" : "Eye of Sauron");
    return 1;
}

Scene scene_eye = {
    "EYE", "eye.cfg", 0, P, (int)(sizeof P / sizeof P[0]),
    init, NULL, update, draw, key,
    "N switch between human eye and Eye of Sauron", NULL
};
