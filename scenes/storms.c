/*
 * storms.c  -  an animated gas giant: cloud bands flowing in alternating
 * jet streams, turbulent edges, and swirling storms that drift with the
 * winds of their band, including a great red spot.
 *
 * Each band of latitude has its own wind speed, so the clouds shear past
 * each other. Left alone, the shear would stretch the clouds into ever
 * finer streaks, so the flow is drawn twice, half a cycle apart, and the
 * two are cross-faded (a "flow map"): it looks endless but never smears.
 *
 * palette 0 Jupiter, 1 Saturn, 2 Neptune
 */
#include "../dome/dome.h"
#include <math.h>

static struct { float palette, wind, bands, turb, storms, bright, sun, atmos; } L =
    { 0.0f, 1.0f, 1.0f, 1.0f, 5.0f, 1.0f, 0.85f, 0.35f };

static Param P[] = {
    { "palette", &L.palette, 1.0f,  0.0f, 2.0f, 0, "0 = Jupiter, 1 = Saturn, 2 = Neptune" },
    { "wind",    &L.wind,    0.1f,  0.0f, 5.0f, 0, "speed of the jet streams" },
    { "bands",   &L.bands,   0.1f,  0.3f, 3.0f, 0, "how many cloud bands" },
    { "turb",    &L.turb,    0.1f,  0.0f, 3.0f, 0, "turbulence along the band edges" },
    { "storms",  &L.storms,  1.0f,  0.0f, 5.0f, 0, "number of storms (the red spot is first)" },
    { "bright",  &L.bright,  0.05f, 0.2f, 2.5f, 0, "overall brightness" },
    { "sun",     &L.sun,     0.05f, 0.0f, 1.0f, 0, "day/night shading: 0 = lit all over" },
    { "atmos",   &L.atmos,   0.05f, 0.0f, 2.0f, 0, "haze glow round the rim" },
};

#define NSTORM 5
/* latitude (deg), longitude (deg), radius (rad), swirl strength */
static const float STORM[NSTORM][4] = {
    { -22.0f,   0.0f, 0.17f,  3.0f },   /* great red spot     */
    { -33.0f,  70.0f, 0.05f, -2.0f },   /* white ovals        */
    { -33.0f, 140.0f, 0.045f, -2.0f },
    {  40.0f, 200.0f, 0.06f,  2.2f },
    {  17.0f, 290.0f, 0.04f, -1.8f },
};

static const float PAL[3][4][3] = {     /* light, dark, accent, storm */
    { { 0.93f, 0.87f, 0.74f }, { 0.72f, 0.46f, 0.28f }, { 0.48f, 0.30f, 0.20f }, { 0.80f, 0.33f, 0.20f } },
    { { 0.94f, 0.88f, 0.68f }, { 0.80f, 0.68f, 0.45f }, { 0.66f, 0.55f, 0.38f }, { 0.98f, 0.95f, 0.85f } },
    { { 0.62f, 0.78f, 0.98f }, { 0.22f, 0.38f, 0.82f }, { 0.12f, 0.20f, 0.55f }, { 0.06f, 0.10f, 0.32f } },
};

static const char *STORMS_GLSL =
    "uniform float u_flow_t, u_wind, u_bands, u_turb, u_bright, u_sun, u_atmos;\n"
    "uniform vec3  u_light, u_dark, u_accent, u_spot;\n"
    "uniform vec4  u_storm[5];\n"            /* centre xyz, radius */
    "uniform float u_swirl[5];\n"
    "uniform float u_nstorm;\n"
    "float h3(vec3 p){ return fract(sin(dot(p, vec3(127.1, 311.7, 74.7))) * 43758.5453); }\n"
    "float n3(vec3 p){\n"
    "  vec3 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);\n"
    "  return mix(mix(mix(h3(i), h3(i + vec3(1,0,0)), f.x),\n"
    "                 mix(h3(i + vec3(0,1,0)), h3(i + vec3(1,1,0)), f.x), f.y),\n"
    "             mix(mix(h3(i + vec3(0,0,1)), h3(i + vec3(1,0,1)), f.x),\n"
    "                 mix(h3(i + vec3(0,1,1)), h3(i + vec3(1,1,1)), f.x), f.y), f.z);\n"
    "}\n"
    "float wind(float lat){ return u_wind * sin(lat * u_bands * 6.0); }\n"
    /* cloud pattern at time t: rotate the point back along its jet stream */
    "float clouds(vec3 d, float lat, float t){\n"
    "  float a = wind(lat) * t, c = cos(a), s = sin(a);\n"
    "  vec3 p = vec3(c * d.x - s * d.z, d.y, s * d.x + c * d.z) * 4.0;\n"
    "  float w = n3(p + 3.1) - 0.5;\n"
    "  float fine = n3(p * 2.7 + 11.0) - 0.5;\n"
    "  return sin(lat * u_bands * 12.0 + (w * 3.2 + fine * 1.1) * u_turb\n"
    "             + sin(lat * 23.0) * 0.6);\n"
    "}\n"
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  vec3 d = dir;\n"
    "  float spot = 0.0, ring = 0.0;\n"
    /* storms: twist the clouds round each storm centre */
    "  for (int k = 0; k < 5; k++) {\n"
    "    if (float(k) >= u_nstorm) break;\n"
    "    vec3 s = u_storm[k].xyz;\n"
    "    float r = u_storm[k].w;\n"
    "    float c = dot(d, s);\n"
    "    if (c < 0.8) continue;\n"
    /* (distance / r)^2, stretched east-west into an oval */
    "    vec3 e = normalize(vec3(s.z, 0.0, -s.x) + 1e-5);\n"
    "    float de = dot(d - s, e);\n"
    "    float q = ((1.0 - c) * 2.0 - 0.6 * de * de) / (r * r);\n"
    "    float ang = u_swirl[k] * exp(-q);\n"
    "    float ca = cos(ang), sa = sin(ang);\n"
    "    d = d * ca + cross(s, d) * sa + s * dot(s, d) * (1.0 - ca);\n"
    "    spot = max(spot, smoothstep(0.9, 0.35, q) * (k == 0 ? 1.0 : 0.85));\n"
    "    ring = max(ring, smoothstep(0.6, 0.9, q) * smoothstep(1.6, 0.9, q));\n"
    "  }\n"
    "  float lat = asin(clamp(d.y, -1.0, 1.0));\n"
    /* two flow phases, half a cycle apart, cross-faded */
    "  float T = 30.0;\n"
    "  float t1 = mod(u_flow_t, T), t2 = mod(u_flow_t + 0.5 * T, T);\n"
    "  float w1 = 1.0 - abs(2.0 * t1 / T - 1.0);\n"
    "  float b = clouds(d, lat, t1 - 0.5 * T) * w1\n"
    "          + clouds(d, lat, t2 - 0.5 * T) * (1.0 - w1);\n"
    "  vec3 col = mix(u_light, u_dark, smoothstep(-0.8, 0.8, b));\n"
    "  col = mix(col, u_accent, smoothstep(0.7, 1.0, b) * 0.5);\n"
    "  col = mix(col, u_dark * 0.8, ring * 0.6);\n"
    "  col = mix(col, u_spot * (0.85 + 0.3 * smoothstep(-1.0, 1.0, b)), spot);\n"
    /* lighting and haze, as in the planet scene */
    "  vec3 sunv = normalize(vec3(-0.6, 0.35, 0.7));\n"
    "  float dd = dot(view, sunv);\n"
    "  col *= mix(1.0, 0.04 + 0.96 * smoothstep(-0.12, 0.35, dd), u_sun) * u_bright;\n"
    "  float rim = pow(1.0 - clamp(view.z, 0.0, 1.0), 3.0);\n"
    "  col += u_light * 0.4 * rim * u_atmos * mix(1.0, smoothstep(-0.3, 0.5, dd), u_sun);\n"
    "  return vec4(col, 1.0);\n"
    "}\n";

static GLuint g_prog;
static float  g_t;

static void init(void) { g_prog = dome_surface_program(STORMS_GLSL); }

static void update(const DomeFrame *f) { g_t += f->dt * 0.03f; }

static void draw(const DomeFrame *f)
{
    GLuint p = g_prog;
    int pal = (int)lroundf(L.palette);
    if (pal < 0) pal = 0;
    if (pal > 2) pal = 2;

    /* storms drift with the wind of their own band. The clouds are drawn
     * at a time that cycles every 30 units; the storms move on the true
     * time, matching the clouds' average drift. */
    float cen[NSTORM][4], sw[NSTORM];
    for (int k = 0; k < NSTORM; k++) {
        float lat = DEG2RAD(STORM[k][0]);
        float w = L.wind * sinf(lat * L.bands * 6.0f);
        float lon = DEG2RAD(STORM[k][1]) + w * g_t;
        float v[3]; dome_sph(lat, lon, v);
        cen[k][0] = v[0]; cen[k][1] = v[1]; cen[k][2] = v[2]; cen[k][3] = STORM[k][2];
        sw[k] = STORM[k][3];
    }

    dome_surface_begin(p, f);
    glUniform1f(glGetUniformLocation(p, "u_flow_t"), g_t);
    glUniform1f(glGetUniformLocation(p, "u_wind"), L.wind);
    glUniform1f(glGetUniformLocation(p, "u_bands"), L.bands);
    glUniform1f(glGetUniformLocation(p, "u_turb"), L.turb);
    glUniform1f(glGetUniformLocation(p, "u_bright"), L.bright);
    glUniform1f(glGetUniformLocation(p, "u_sun"), L.sun);
    glUniform1f(glGetUniformLocation(p, "u_atmos"), L.atmos);
    glUniform3fv(glGetUniformLocation(p, "u_light"), 1, PAL[pal][0]);
    glUniform3fv(glGetUniformLocation(p, "u_dark"), 1, PAL[pal][1]);
    glUniform3fv(glGetUniformLocation(p, "u_accent"), 1, PAL[pal][2]);
    glUniform3fv(glGetUniformLocation(p, "u_spot"), 1, PAL[pal][3]);
    glUniform4fv(glGetUniformLocation(p, "u_storm[0]"), NSTORM, &cen[0][0]);
    glUniform1fv(glGetUniformLocation(p, "u_swirl[0]"), NSTORM, sw);
    glUniform1f(glGetUniformLocation(p, "u_nstorm"), floorf(L.storms + 0.5f));
    dome_surface_end();
}

Scene scene_storms = {
    "GAS GIANT", "storms.cfg", 0, P, (int)(sizeof P / sizeof P[0]),
    init, NULL, update, draw, NULL, NULL, NULL
};
