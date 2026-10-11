/*
 * julia.c  -  a Julia set wrapped round the globe on the Riemann sphere.
 *
 * Mathematicians treat the complex plane as a sphere: each point of the
 * globe is projected from the north pole onto the plane (stereographic
 * projection), so zero is at the south pole and infinity at the north.
 * The fractal then covers the whole globe with no seam. The constant c
 * walks slowly along the edge of the Mandelbrot set's main cardioid,
 * where every Julia set is connected and full of spirals, so the shape
 * keeps morphing without ever breaking up into dust.
 *
 * N steps through the shared colour themes. M switches to the Mandelbrot
 * set, the map of every Julia set, wrapped round the globe the same way.
 */
#include "../dome/dome.h"
#include <math.h>
#include <stdio.h>

static struct { float theme, set, edge, speed, iter, zoom, hue, bands, glow; } L =
    { 0.0f, 0.0f, 0.99f, 1.0f, 64.0f, 1.0f, 0.0f, 1.0f, 1.0f };

static Param P[] = {
    { "theme",  &L.theme,  1.0f, 0.0f, 7.0f, 0,
      "0 rainbow, 1 fire, 2 ice, 3 neon, 4 aurora, 5 gold, 6 amber, 7 jewel (N)" },
    { "set",    &L.set,    1.0f, 0.0f, 1.0f, 0, "0 Julia set, 1 Mandelbrot set (M)" },
    { "edge",   &L.edge,   0.005f, 0.80f, 1.10f, 0,
      "how close c walks to the Mandelbrot edge: under 1 solid, over 1 lacy" },
    { "speed",  &L.speed,  0.1f,  0.0f, 5.0f, 0, "how fast c travels (0 = frozen)" },
    { "iter",   &L.iter,   4.0f,  8.0f, 200.0f, 0,
      "detail: more iterations = finer filaments, slower" },
    { "zoom",   &L.zoom,   0.05f, 0.2f, 4.0f, 0, "scale of the set on the globe" },
    { "hue",    &L.hue,    0.02f, 0.0f, 1.0f, 0, "shift the colours round the wheel" },
    { "bands",  &L.bands,  0.1f,  0.1f, 5.0f, 0, "how quickly colours cycle" },
    { "glow",   &L.glow,   0.05f, 0.0f, 2.0f, 0, "brightness of the inside of the set" },
};

static const char *JULIA_GLSL =
    "uniform vec2  u_c;\n"
    "uniform float u_iter, u_zoom, u_hue, u_bands, u_glow, u_theme, u_set;\n"
    "vec3 tp(float t){ return theme(u_theme, t); }\n"
    "vec4 surface(vec3 dir, vec3 view){\n"
    /* stereographic projection from the north pole */
    "  vec2 z = vec2(dir.x, dir.z) / max(1.0 - dir.y, 1e-4) * u_zoom;\n"
    /* Mandelbrot: this point is c, and z starts at zero */
    "  vec2 cc = u_c;\n"
    "  if (u_set > 0.5) { cc = z * 0.85 - vec2(0.55, 0.0); z = vec2(0.0); }\n"
    "  float n = 0.0, trap = 1e9, m = dot(z, z);\n"
    "  for (int k = 0; k < 200; k++) {\n"
    "    if (float(k) >= u_iter || m > 256.0) break;\n"
    "    z = vec2(z.x * z.x - z.y * z.y, 2.0 * z.x * z.y) + cc;\n"
    "    m = dot(z, z);\n"
    "    trap = min(trap, m);\n"
    "    n += 1.0;\n"
    "  }\n"
    "  vec3 col;\n"
    "  if (m <= 256.0) {\n"
    /* inside the set: lit by how close the orbit came to zero */
    "    col = tp(u_hue + 0.55 + sqrt(trap) * 0.9) * (0.25 + 0.75 * exp(-trap * 2.0)) * u_glow;\n"
    "  } else {\n"
    /* outside: smooth escape count, so colour flows without banding */
    "    float s = n - log2(max(log2(m) * 0.5, 1e-4));\n"
    "    col = tp(u_hue + s * 0.09 * u_bands) * pow(clamp(s / 22.0, 0.0, 1.0), 0.8);\n"
    "  }\n"
    "  return vec4(col, 1.0);\n"
    "}\n";

static GLuint g_prog;

static void init(void) { g_prog = dome_surface_program_themed(JULIA_GLSL); }

static float  g_phase = 2.0f;
static void update(const DomeFrame *f) { g_phase += f->dt * 0.05f * L.speed; }

static void draw(const DomeFrame *f)
{
    GLuint p = g_prog;
    dome_surface_begin(p, f);
    /* main cardioid: c = e^ia / 2 - e^2ia / 4 */
    float cx = 0.5f * cosf(g_phase) - 0.25f * cosf(2.0f * g_phase);
    float cy = 0.5f * sinf(g_phase) - 0.25f * sinf(2.0f * g_phase);
    glUniform2f(glGetUniformLocation(p, "u_c"), L.edge * cx, L.edge * cy);
    glUniform1f(glGetUniformLocation(p, "u_iter"), L.iter);
    glUniform1f(glGetUniformLocation(p, "u_zoom"), L.zoom);
    glUniform1f(glGetUniformLocation(p, "u_hue"), L.hue);
    glUniform1f(glGetUniformLocation(p, "u_bands"), L.bands);
    glUniform1f(glGetUniformLocation(p, "u_glow"), L.glow);
    glUniform1f(glGetUniformLocation(p, "u_theme"), floorf(L.theme + 0.5f));
    glUniform1f(glGetUniformLocation(p, "u_set"), floorf(L.set + 0.5f));
    dome_surface_end();
}

static int key(SDL_Keycode k, int shift)
{
    (void)shift;
    if (k == SDLK_n) {
        L.theme = (float)(((int)lroundf(L.theme) + 1) % DOME_THEMES);
        printf("Julia: %s\n", DOME_THEME_NAMES[(int)L.theme]);
        return 1;
    }
    if (k == SDLK_m) {
        L.set = L.set < 0.5f ? 1.0f : 0.0f;
        printf("Julia: %s set\n", L.set > 0.5f ? "Mandelbrot" : "Julia");
        return 1;
    }
    return 0;
}

Scene scene_julia = {
    "JULIA", "julia.cfg", 0, P, (int)(sizeof P / sizeof P[0]),
    init, NULL, update, draw, key, "N next colour theme, M Julia / Mandelbrot", NULL
};
