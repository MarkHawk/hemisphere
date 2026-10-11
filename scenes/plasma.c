/*
 * plasma.c  -  a procedural colour field: the smallest possible scene.
 * N steps through the shared colour themes (rainbow, fire, ice, neon,
 * aurora, gold, amber, jewel).
 * Copy this file to start a new one: write surface(), list some Params,
 * add it to main.c.
 */
#include "../dome/dome.h"
#include <math.h>
#include <stdio.h>

static struct { float theme, speed, scale, bands, sat, bright; } L =
    { 0.0f, 1.0f, 1.5f, 1.0f, 1.0f, 1.0f };

static Param P[] = {
    { "theme",  &L.theme,  1.0f, 0.0f, 7.0f, 0,
      "0 rainbow, 1 fire, 2 ice, 3 neon, 4 aurora, 5 gold, 6 amber, 7 jewel (N)" },
    { "speed",  &L.speed,  0.1f, 0.0f, 5.0f, 0, "how fast the colours flow" },
    { "scale",  &L.scale,  0.1f, 0.3f, 6.0f, 0, "size of the blobs (bigger = smaller blobs)" },
    { "bands",  &L.bands,  0.1f, 0.2f, 4.0f, 0, "how many colour bands" },
    { "sat",    &L.sat,    0.05f, 0.0f, 1.5f, 0, "colour saturation" },
    { "bright", &L.bright, 0.05f, 0.1f, 2.0f, 0, "brightness" },
};

static const char *PLASMA_GLSL =
    "uniform float u_speed, u_scale, u_bands, u_sat, u_bright, u_theme;\n"
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  float t = u_time * u_speed;\n"
    "  vec3 p = dir * u_scale;\n"
    "  float v = sin(p.x * 3.0 + t) + sin(p.y * 4.0 - t * 1.3)\n"
    "          + sin(p.z * 5.0 + t * 0.7)\n"
    "          + sin(length(p.xy + vec2(sin(t * 0.3), cos(t * 0.4))) * 4.0);\n"
    "  vec3 c = theme(u_theme, (v * u_bands + t * 0.5) / 6.28318);\n"
    "  c = mix(vec3(dot(c, vec3(0.333))), c, u_sat);\n"
    "  return vec4(c * u_bright, 1.0);\n"
    "}\n";

static GLuint g_prog;

static void init(void) { g_prog = dome_surface_program_themed(PLASMA_GLSL); }

static void draw(const DomeFrame *f)
{
    GLuint p = g_prog;
    dome_surface_begin(p, f);
    glUniform1f(glGetUniformLocation(p, "u_speed"), L.speed);
    glUniform1f(glGetUniformLocation(p, "u_scale"), L.scale);
    glUniform1f(glGetUniformLocation(p, "u_bands"), L.bands);
    glUniform1f(glGetUniformLocation(p, "u_sat"), L.sat);
    glUniform1f(glGetUniformLocation(p, "u_bright"), L.bright);
    glUniform1f(glGetUniformLocation(p, "u_theme"), floorf(L.theme + 0.5f));
    dome_surface_end();
}

static int key(SDL_Keycode k, int shift)
{
    (void)shift;
    if (k != SDLK_n) return 0;
    L.theme = (float)(((int)lroundf(L.theme) + 1) % DOME_THEMES);
    printf("Plasma: %s\n", DOME_THEME_NAMES[(int)L.theme]);
    return 1;
}

Scene scene_plasma = {
    "PLASMA", "plasma.cfg", 0, P, (int)(sizeof P / sizeof P[0]),
    init, NULL, NULL, draw, key, "N next colour theme", NULL
};
