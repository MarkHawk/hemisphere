/*
 * kaleido.c  -  a kaleidoscope sphere.
 *
 * Every point of the globe is folded into one small triangle by mirrors,
 * then coloured with a folding fractal (the "kaliset": repeatedly take
 * abs(p) / |p|^2 and shift it). The pattern inside the mirrors slowly
 * turns and breathes, so the mandala keeps changing.
 *
 *   style 0  cube mirrors: 48 identical triangles over the globe
 *   style 1  classic kaleidoscope: 'mirrors' wedges round the pole
 *
 * N steps through the shared colour themes, M switches the mirror style.
 */
#include "../dome/dome.h"
#include <math.h>
#include <stdio.h>

static struct { float theme, style, mirrors, fold, depth, speed, hue, sat, bright; } L =
    { 0.0f, 0.0f, 8.0f, 0.62f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f };

static Param P[] = {
    { "theme",   &L.theme,   1.0f,  0.0f, 7.0f, 0,
      "0 rainbow, 1 fire, 2 ice, 3 neon, 4 aurora, 5 gold, 6 amber, 7 jewel (N)" },
    { "style",   &L.style,   1.0f,  0.0f, 1.0f, 0,
      "0 = cube mirrors (48 tiles), 1 = wedges round the pole" },
    { "mirrors", &L.mirrors, 1.0f,  3.0f, 24.0f, 0, "style 1: number of wedges" },
    { "fold",    &L.fold,    0.01f, 0.30f, 1.20f, 0,
      "shape of the fractal: small changes give very different patterns" },
    { "depth",   &L.depth,   0.1f,  0.3f, 3.0f, 0, "zoom into the pattern" },
    { "speed",   &L.speed,   0.1f,  0.0f, 5.0f, 0, "how fast the pattern turns and breathes" },
    { "hue",     &L.hue,     0.02f, 0.0f, 1.0f, 0, "shift the colours round the wheel" },
    { "sat",     &L.sat,     0.05f, 0.0f, 1.5f, 0, "colour saturation" },
    { "bright",  &L.bright,  0.05f, 0.1f, 2.0f, 0, "brightness" },
};

static const char *KALEIDO_GLSL =
    "uniform mat3  u_twist;\n"
    "uniform float u_style, u_mirrors, u_fold, u_depth, u_hue, u_sat, u_bright, u_theme;\n"
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  vec3 p = dir;\n"
    "  if (u_style < 0.5) {\n"
    /* three mirror planes, then sort: lands in 1 of 48 triangles */
    "    p = abs(p);\n"
    "    if (p.x < p.y) p.xy = p.yx;\n"
    "    if (p.y < p.z) p.yz = p.zy;\n"
    "    if (p.x < p.y) p.xy = p.yx;\n"
    "  } else {\n"
    "    float seg = 2.0 * PI / u_mirrors;\n"
    "    float a = atan(p.x, p.z);\n"
    "    a = abs(mod(a + 0.5 * seg, seg) - 0.5 * seg);\n"
    "    float r = length(p.xz);\n"
    "    p = vec3(r * sin(a), abs(p.y), r * cos(a));\n"
    "  }\n"
    /* the pattern turns inside the mirrors */
    "  vec3 q = (u_twist * p) * u_depth;\n"
    "  vec3 k = vec3(u_fold, u_fold * 0.92, u_fold * 1.07);\n"
    "  float acc = 0.0, last = 0.0;\n"
    "  for (int i = 0; i < 9; i++) {\n"
    "    q = abs(q) / max(dot(q, q), 1e-3) - k;\n"
    "    float m = length(q);\n"
    "    acc += exp(-abs(m - last) * 4.0);\n"
    "    last = m;\n"
    "  }\n"
    "  float t = acc / 9.0;\n"
    /* the other themes have dark stretches of their own, so they cycle
     * more slowly through the pattern and aren't dimmed further */
    "  vec3 c;\n"
    "  if (u_theme < 0.5) {\n"
    "    c = theme(u_theme, u_hue + t * 2.8 + last * 0.25);\n"
    "    c *= (0.30 + 0.70 * smoothstep(0.05, 0.7, t)) * 1.25;\n"
    "  } else {\n"
    /* the busiest parts of the fractal take the theme's brightest colour,
     * the quiet parts its darkest */
    "    c = theme(u_theme, u_hue + (t * 2.8 + last * 0.25) * 0.6) * 1.35;\n"
    "  }\n"
    "  c = mix(vec3(dot(c, vec3(0.333))), c, u_sat);\n"
    "  return vec4(c * u_bright, 1.0);\n"
    "}\n";

static GLuint g_prog;
static float  g_t;

static void init(void) { g_prog = dome_surface_program_themed(KALEIDO_GLSL); }

static void update(const DomeFrame *f) { g_t += f->dt * L.speed; }

static void draw(const DomeFrame *f)
{
    GLuint p = g_prog;
    /* slow tumble of the pattern inside the mirrors */
    mat3 a, b, tw;
    m3_rx(a, 0.21f * g_t);
    m3_rz(b, 0.13f * g_t);
    m3_mul(tw, a, b);
    dome_surface_begin(p, f);
    glUniformMatrix3fv(glGetUniformLocation(p, "u_twist"), 1, GL_FALSE, tw);
    glUniform1f(glGetUniformLocation(p, "u_style"), L.style);
    glUniform1f(glGetUniformLocation(p, "u_mirrors"), floorf(L.mirrors + 0.5f));
    /* the fractal breathes a little round the chosen shape */
    glUniform1f(glGetUniformLocation(p, "u_fold"), L.fold + 0.03f * sinf(0.4f * g_t));
    glUniform1f(glGetUniformLocation(p, "u_depth"), L.depth);
    glUniform1f(glGetUniformLocation(p, "u_hue"), L.hue);
    glUniform1f(glGetUniformLocation(p, "u_theme"), floorf(L.theme + 0.5f));
    glUniform1f(glGetUniformLocation(p, "u_sat"), L.sat);
    glUniform1f(glGetUniformLocation(p, "u_bright"), L.bright);
    dome_surface_end();
}

static int key(SDL_Keycode k, int shift)
{
    (void)shift;
    if (k == SDLK_n) {
        L.theme = (float)(((int)lroundf(L.theme) + 1) % DOME_THEMES);
        printf("Kaleidoscope: %s\n", DOME_THEME_NAMES[(int)L.theme]);
        return 1;
    }
    if (k == SDLK_m) {
        L.style = L.style < 0.5f ? 1.0f : 0.0f;
        printf("Kaleidoscope: %s\n", L.style > 0.5f ? "wedges" : "cube mirrors");
        return 1;
    }
    return 0;
}

Scene scene_kaleido = {
    "KALEIDOSCOPE", "kaleido.cfg", 0, P, (int)(sizeof P / sizeof P[0]),
    init, NULL, update, draw, key, "N next colour theme, M mirror style", NULL
};
