/*
 * hypno.c  -  hypnotic geometry. N steps through the styles:
 *
 *   0 tesseract       a 4D cube turning through the fourth dimension
 *   1 quasicrystal    Penrose-style five-fold pattern, never repeating
 *   2 moire           fine rings round two drifting centres, interfering
 *   3 Fibonacci globe seeds spiralling pole to pole, like a pine cone
 *   4 truchet maze    quarter-circle tiles flipping, the paths re-routing
 *   5 hypno spiral    a two-armed spiral turning inwards
 *   6 tunnel          an endless chequered tunnel rushing towards you
 *   7 pendulum        a glowing pendulum tracing rosettes over the globe
 *   8 hexagons        a honeycomb breathing in waves from the centre
 *   9 geodesic        a geodesic sphere, with a smaller one turning inside
 *
 * M picks the colours: 0 each style's own, 1 to 8 the shared themes.
 * The moire, Fibonacci globe, pendulum and geodesic turn with the ball and
 * cover the whole sphere; the rest face you. Styles 0, 7 and 9 are drawn as lines of dots over a
 * dark background; the rest are one small shader each.
 */
#include "../dome/dome.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NSTYLE 10
static const char *NAMES[NSTYLE] = {
    "tesseract", "quasicrystal", "moire", "Fibonacci globe", "truchet maze",
    "hypno spiral", "tunnel", "pendulum", "hexagons", "geodesic"
};

static struct { float style, theme, speed, scale, bright; } L =
    { 0.0f, 0.0f, 1.0f, 1.0f, 1.0f };

static Param P[] = {
    { "style",  &L.style,  1.0f, 0.0f, 9.0f, 0,
      "0 tesseract, 1 quasicrystal, 2 moire, 3 Fibonacci globe, 4 truchet, 5 spiral, "
      "6 tunnel, 7 pendulum, 8 hexagons, 9 geodesic (N)" },
    { "theme",  &L.theme,  1.0f, 0.0f, 8.0f, 0,
      "0 classic, 1 rainbow, 2 fire, 3 ice, 4 neon, 5 aurora, 6 gold, 7 amber, 8 jewel (M)" },
    { "speed",  &L.speed,  0.1f, 0.0f, 4.0f, 0, "speed of the animation" },
    { "scale",  &L.scale,  0.05f, 0.4f, 2.5f, 0, "size of the pattern (bigger = finer)" },
    { "bright", &L.bright, 0.05f, 0.2f, 2.0f, 0, "brightness" },
};

static const char LIB[] =
    "uniform float u_t, u_scale, u_bright, u_theme;\n"
    "vec2 rot(vec2 p, float a){ float c = cos(a), s = sin(a); return vec2(c * p.x - s * p.y, s * p.x + c * p.y); }\n"
    /* a style's own colour (c), or the shared theme at x when M picks one */
    "vec3 hc(vec3 c, float x){ return u_theme > 0.5 ? theme(u_theme - 1.0, x) : c; }\n"
    /* the front of the dome as a flat plane: 0 at the centre, about 1 at the rim */
    "vec2 plane(vec3 v){ return v.xy / (1.0 + max(v.z, 0.0)); }\n";

/* ---- background for the dot styles ---- */
static const char BG[] =
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  float a = acos(clamp(view.z, -1.0, 1.0));\n"
    "  vec3 c = hc(vec3(0.10, 0.04, 0.20), 0.6) * 0.35 * (1.0 - smoothstep(0.0, 1.4, a));\n"
    "  return vec4(c * u_bright, 1.0);\n"
    "}\n";

/* ---- 1: quasicrystal: five plane waves at 36 degree steps ---- */
static const char QUASI[] =
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  vec2 q = rot(plane(view), u_t * 0.03) * 30.0 * u_scale;\n"
    "  float v = 0.0;\n"
    "  for (int i = 0; i < 5; i++) {\n"
    "    float a = float(i) * 0.6283185;\n"
    "    v += cos(dot(q, vec2(cos(a), sin(a))) + u_t * 0.8);\n"
    "  }\n"
    "  float b = v * 0.5 + 2.5;\n"
    "  float f = fract(b);\n"
    "  vec3 c = hc(0.5 + 0.5 * cos(6.2832 * (b * 0.15 + vec3(0.0, 0.33, 0.67))), b * 0.15);\n"
    "  c *= smoothstep(0.0, 0.12, min(f, 1.0 - f)) * 0.8 + 0.2;\n"
    "  return vec4(c * u_bright, 1.0);\n"
    "}\n";

/* ---- 2: moire (turns with the ball) ---- */
static const char MOIRE[] =
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  vec3 A = normalize(vec3(sin(u_t * 0.11) * 0.5, cos(u_t * 0.09) * 0.4, 1.0));\n"
    "  vec3 B = normalize(vec3(cos(u_t * 0.07) * 0.6, sin(u_t * 0.13) * 0.5, 1.0));\n"
    "  float k = 38.0 * u_scale;\n"
    "  float a1 = acos(clamp(dot(dir, A), -1.0, 1.0)) * k;\n"
    "  float a2 = acos(clamp(dot(dir, B), -1.0, 1.0)) * k;\n"
    "  float l1 = smoothstep(0.30, 0.15, abs(fract(a1) - 0.5));\n"
    "  float l2 = smoothstep(0.30, 0.15, abs(fract(a2) - 0.5));\n"
    "  vec3 base = hc(vec3(0.95, 0.92, 0.85), 0.5 + 0.5 * sin(u_t * 0.1 + dir.y));\n"
    "  vec3 c = base * (1.0 - 0.85 * max(l1, l2));\n"
    "  return vec4(c * u_bright, 1.0);\n"
    "}\n";

/* ---- 3: Fibonacci globe. 610 seeds spread evenly over the sphere, seed
 * i at height 1 - (2i + 1) / 610 and turned i golden angles round. Seen
 * together they form two families of spirals, 21 one way and 34 the other:
 * each phase below is a whole number exactly at every seed. ---- */
static const char FIBGLOBE[] =
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  float z = dir.y;\n"
    "  float th = atan(dir.z, dir.x) + u_t * 0.15 + z * sin(u_t * 0.2) * 1.2;\n"   /* slow twist */
    "  float i = 610.0 * (1.0 - z) * 0.5;\n"
    "  float th2 = th / 6.2832;\n"
    "  float s1 = th2 * 21.0 - i * (21.0 * 0.3819660 - 8.0);\n"
    "  float s2 = th2 * 34.0 - i * (34.0 * 0.3819660 - 13.0);\n"
    "  float seed = (0.5 + 0.5 * cos(6.2832 * s1)) * (0.5 + 0.5 * cos(6.2832 * s2));\n"
    "  float pulse = 0.5 + 0.5 * sin(i * 0.04 * u_scale - u_t * 1.5);\n"            /* a wave pole to pole */
    "  vec3 c0 = hc(mix(vec3(0.95, 0.65, 0.15), vec3(0.85, 0.25, 0.10), 0.5 + 0.5 * z), i / 610.0 + u_t * 0.02);\n"
    "  vec3 c = mix(vec3(0.05, 0.03, 0.02), c0 * (0.55 + 0.45 * pulse), smoothstep(0.25, 0.6, seed));\n"
    "  return vec4(c * u_bright, 1.0);\n"
    "}\n";

/* ---- 4: truchet maze ---- */
static const char TRUCHET[] =
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  vec2 q = rot(plane(view), u_t * 0.02) * 9.0 * u_scale;\n"
    "  vec2 cell = floor(q), f = fract(q);\n"
    "  float flip = step(0.5, h3(vec3(cell, floor(u_t * 0.35 + h3(vec3(cell, 4.0)) * 3.0))));\n"
    "  if (flip > 0.5) f.x = 1.0 - f.x;\n"
    "  float d = min(abs(length(f) - 0.5), abs(length(f - 1.0) - 0.5));\n"
    "  float x = length(q) * 0.04 - u_t * 0.05;\n"
    "  vec3 lc = hc(0.5 + 0.5 * cos(6.2832 * (x + vec3(0.0, 0.33, 0.67))), x);\n"
    "  vec3 c = vec3(0.03, 0.03, 0.06);\n"
    "  c = mix(c, lc * 0.35, smoothstep(0.22, 0.14, d));\n"                /* glow */
    "  c = mix(c, lc, smoothstep(0.11, 0.07, d));\n"
    "  return vec4(c * u_bright, 1.0);\n"
    "}\n";

/* ---- 5: hypno spiral ---- */
static const char SPIRAL[] =
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  float a = acos(clamp(view.z, -1.0, 1.0)), th = atan(view.y, view.x);\n"
    "  float s = th / 6.2832 * 2.0 + a * 5.0 * u_scale - u_t * 0.6;\n"
    "  float f = fract(s);\n"
    "  vec3 c1 = hc(vec3(0.02), fract(s * 0.5) * 0.5);\n"
    "  vec3 c2 = hc(vec3(0.95), fract(s * 0.5) * 0.5 + 0.5);\n"
    "  vec3 c = mix(c1, c2, smoothstep(0.46, 0.54, f) - smoothstep(0.96, 1.0, f));\n"
    "  c *= smoothstep(0.0, 0.12, a) * 0.6 + 0.4;\n"
    "  return vec4(c * u_bright, 1.0);\n"
    "}\n";

/* ---- 6: tunnel ---- */
static const char TUNNEL[] =
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  float a = acos(clamp(view.z, -1.0, 1.0)), th = atan(view.y, view.x) + u_t * 0.1;\n"
    "  float z = 0.35 / (a + 0.03) + u_t * 1.5;\n"
    "  float u = th / 6.2832 * 12.0 * u_scale;\n"
    "  float ring = floor(z * 2.0);\n"
    "  float ck = mod(floor(u) + ring, 2.0);\n"
    "  vec3 c = hc(0.5 + 0.5 * cos(6.2832 * (ring * 0.07 + vec3(0.0, 0.33, 0.67))), ring * 0.07);\n"
    "  c *= 0.35 + 0.65 * ck;\n"
    "  c *= smoothstep(0.02, 0.7, a);\n"                                    /* dark far end */
    "  return vec4(c * u_bright, 1.0);\n"
    "}\n";

/* ---- 9: colour waves behind the geodesic lines ---- */
static const char GEOBG[] =
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  vec3 A = normalize(vec3(sin(u_t * 0.2), 0.5, cos(u_t * 0.2)));\n"
    "  float w = 0.5 + 0.5 * sin(acos(clamp(dot(dir, A), -1.0, 1.0)) * 6.0 * u_scale - u_t * 2.0);\n"
    "  float x = w * 0.3 + u_t * 0.03;\n"
    "  vec3 c = hc(0.5 + 0.5 * cos(6.2832 * (x + vec3(0.0, 0.33, 0.67))), x) * (0.08 + 0.30 * w * w);\n"
    "  return vec4(c * u_bright, 1.0);\n"
    "}\n";

/* ---- 8: breathing hexagons ---- */
static const char HEXES[] =
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  vec2 p = plane(view) * 7.0 * u_scale;\n"
    "  vec2 s = vec2(1.0, 1.7320508);\n"
    "  vec4 h4 = floor(vec4(p, p - vec2(0.5, 1.0)) / s.xyxy) + 0.5;\n"
    "  vec2 a = p - h4.xy * s, b = p - (h4.zw + 0.5) * s;\n"
    "  vec2 lp = dot(a, a) < dot(b, b) ? a : b;\n"
    "  vec2 id = dot(a, a) < dot(b, b) ? h4.xy : h4.zw + 0.5;\n"
    "  float D = length(id * s);\n"
    "  vec2 ap = abs(lp);\n"
    "  float hd = max(dot(ap, s * 0.5), ap.x);\n"                          /* hex distance */
    "  float size = 0.5 * (0.55 + 0.4 * sin(u_t * 1.5 - D * 0.8));\n"
    "  vec3 col = hc(0.5 + 0.5 * cos(6.2832 * (D * 0.08 - u_t * 0.05 + vec3(0.0, 0.33, 0.67))), D * 0.08 - u_t * 0.05);\n"
    "  vec3 c = vec3(0.03);\n"
    "  c = mix(c, col * 0.4, smoothstep(size + 0.08, size, hd));\n"
    "  c = mix(c, col, smoothstep(size, size - 0.03, hd));\n"
    "  return vec4(c * u_bright, 1.0);\n"
    "}\n";

static const char *GLSL[NSTYLE] = {
    BG, QUASI, MOIRE, FIBGLOBE, TRUCHET, SPIRAL, TUNNEL, BG, HEXES, GEOBG
};
static GLuint g_prog[NSTYLE];
static float  g_t;

/* ---------------------------------------------------------------- */
/* Dot-line helpers                                                  */
/* ---------------------------------------------------------------- */
static DomePoints g_lines, g_verts;

static void colour(float x, float *r, float *g, float *b)
{
    *r = 0.5f + 0.5f * cosf(6.2832f * x);
    *g = 0.5f + 0.5f * cosf(6.2832f * (x + 0.33f));
    *b = 0.5f + 0.5f * cosf(6.2832f * (x + 0.67f));
}

/* a straight line drawn on the front of the dome, as if on a flat plate */
static void plate_line(DomePoints *p, float x0, float y0, float x1, float y1,
                       float r, float g, float b, float a)
{
    float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
    int n = (int)(len / 0.006f) + 1;
    for (int i = 0; i <= n; i++) {
        float t = (float)i / n, x = x0 + t * dx, y = y0 + t * dy;
        float rr = x * x + y * y;
        if (rr > 0.97f) continue;
        dome_points_push(p, x, y, sqrtf(1.0f - rr), r, g, b, a);
    }
}

/* ---- 0: tesseract ---- */
static void tesseract(float t)
{
    float v[16][4], q[16][3];
    for (int i = 0; i < 16; i++)
        for (int k = 0; k < 4; k++) v[i][k] = (i >> k) & 1 ? 1.0f : -1.0f;
    float a = t * 0.31f, b = t * 0.23f, c = t * 0.17f;
    for (int i = 0; i < 16; i++) {
        float x = v[i][0], y = v[i][1], z = v[i][2], w = v[i][3], tmp;
        tmp = x * cosf(a) - w * sinf(a); w = x * sinf(a) + w * cosf(a); x = tmp;   /* XW */
        tmp = y * cosf(b) - w * sinf(b); w = y * sinf(b) + w * cosf(b); y = tmp;   /* YW */
        tmp = x * cosf(c) - z * sinf(c); z = x * sinf(c) + z * cosf(c); x = tmp;   /* XZ */
        float s = 1.0f / (4.0f - w);                         /* 4D to 3D */
        x *= s; y *= s; z *= s;
        float s2 = 0.95f / (2.6f - z) / L.scale;             /* 3D to the plate */
        q[i][0] = x * s2 * 3.6f; q[i][1] = y * s2 * 3.6f; q[i][2] = w;
    }
    for (int i = 0; i < 16; i++)
        for (int k = 0; k < 4; k++) {
            int j = i ^ (1 << k);
            if (j < i) continue;
            float r, g, bb;
            float x = (q[i][2] + q[j][2]) * 0.25f + 0.5f;
            if (L.theme < 0.5f) colour(0.55f + x * 0.4f, &r, &g, &bb); else colour(x, &r, &g, &bb);
            plate_line(&g_lines, q[i][0], q[i][1], q[j][0], q[j][1], r, g, bb, 1.0f);
        }
}

/* ---- 7: pendulum: two slow swings, one up and down the globe and one
 * round it, at speeds that never line up, so the trail covers the sphere
 * with rosettes. The last 90 seconds of the path are drawn, fading. ---- */
static void pendulum(float t)
{
    const int N = 6000;
    const float T = 90.0f;
    for (int k = 0; k <= N; k++) {
        float tau = t - T * (1.0f - (float)k / N);
        float th = 1.5708f + 1.30f * sinf(0.53f * tau);
        float ph = 0.37f * tau + 0.6f * sinf(0.21f * tau);
        float x = sinf(th) * cosf(ph), y = cosf(th), z = sinf(th) * sinf(ph);
        float r, g, b, age = (float)k / N;
        colour(tau * 0.02f, &r, &g, &b);
        dome_points_push(&g_lines, x, y, z, r, g, b, 0.2f + 0.8f * age * age);
        if (k == N) {
            dome_points_clear(&g_verts);
            dome_points_push(&g_verts, x, y, z, 1.0f, 1.0f, 1.0f, 1.0f);
        }
    }
}

/* ---- 9: geodesic spheres: an icosahedron with each face cut into small
 * triangles, pushed out onto the sphere. A coarser one sits inside,
 * drawn recessed so it shows depth, turning the other way. ---- */
static DomePoints g_geo_out, g_geo_in;

static void geodesic(DomePoints *p, int freq, float radius, float r, float g, float b)
{
    const float F = 1.6180340f;
    float v[12][3];
    int n = 0;
    for (int s1 = -1; s1 <= 1; s1 += 2)
        for (int s2 = -1; s2 <= 1; s2 += 2) {
            float a[3][3] = { { 0, (float)s1, s2 * F }, { (float)s1, s2 * F, 0 }, { s2 * F, 0, (float)s1 } };
            for (int k = 0; k < 3; k++) memcpy(v[n++], a[k], sizeof a[k]);
        }
    for (int i = 0; i < 12; i++) {
        float l = sqrtf(v[i][0] * v[i][0] + v[i][1] * v[i][1] + v[i][2] * v[i][2]);
        for (int k = 0; k < 3; k++) v[i][k] /= l;
    }
    float edge = 0;                               /* shortest vertex distance */
    for (int j = 1; j < 12; j++) {
        float dx = v[0][0] - v[j][0], dy = v[0][1] - v[j][1], dz = v[0][2] - v[j][2];
        float d = dx * dx + dy * dy + dz * dz;
        if (edge == 0 || d < edge) edge = d;
    }
#define NEAR(a, b) ((v[a][0]-v[b][0])*(v[a][0]-v[b][0]) + (v[a][1]-v[b][1])*(v[a][1]-v[b][1]) + (v[a][2]-v[b][2])*(v[a][2]-v[b][2]) < edge * 1.01f)
    for (int a = 0; a < 12; a++)
        for (int b2 = a + 1; b2 < 12; b2++)
            for (int c = b2 + 1; c < 12; c++) {
                if (!NEAR(a, b2) || !NEAR(b2, c) || !NEAR(a, c)) continue;
                /* the face a, b2, c: points (i, j) across it */
                for (int i = 0; i <= freq; i++)
                    for (int j = 0; i + j <= freq; j++) {
                        float P[3][3];
                        int nb[3][2] = { { i + 1, j }, { i, j + 1 }, { i + 1, j - 1 } };
                        for (int e = 0; e < 3; e++) {
                            int ii = nb[e][0], jj = nb[e][1];
                            if (ii < 0 || jj < 0 || ii + jj > freq) continue;
                            if (e == 2 && j == 0) continue;
                            int pts[2][2] = { { i, j }, { ii, jj } };
                            for (int q = 0; q < 2; q++) {
                                float w0 = (float)(freq - pts[q][0] - pts[q][1]), w1 = (float)pts[q][0], w2 = (float)pts[q][1];
                                float x = v[a][0] * w0 + v[b2][0] * w1 + v[c][0] * w2;
                                float y = v[a][1] * w0 + v[b2][1] * w1 + v[c][1] * w2;
                                float z = v[a][2] * w0 + v[b2][2] * w1 + v[c][2] * w2;
                                float l = sqrtf(x * x + y * y + z * z) / radius;
                                P[q][0] = x / l; P[q][1] = y / l; P[q][2] = z / l;
                            }
                            dome_points_line(p, P[0], P[1], r, g, b, 1.0f, 0.006f);
                        }
                    }
            }
#undef NEAR
    dome_points_upload(p);
}

/* ---------------------------------------------------------------- */
static void init(void)
{
    for (int i = 0; i < NSTYLE; i++) {
        if (i > 0 && GLSL[i] == BG) { g_prog[i] = g_prog[0]; continue; }
        size_t n = strlen(LIB) + strlen(GLSL[i]) + 1;
        char *src = malloc(n);
        snprintf(src, n, "%s%s", LIB, GLSL[i]);
        g_prog[i] = dome_surface_program_themed(src);
        free(src);
    }
    dome_points_init(&g_lines, 60000);
    dome_points_init(&g_verts, 64);
    dome_points_init(&g_geo_out, 60000);
    dome_points_init(&g_geo_in, 20000);
    geodesic(&g_geo_out, 4, 1.0f, 0.55f, 0.95f, 1.0f);
    geodesic(&g_geo_in, 2, 0.80f, 1.0f, 0.75f, 0.35f);
}

static int style(void)
{
    int s = (int)lroundf(L.style);
    return s < 0 ? 0 : s >= NSTYLE ? NSTYLE - 1 : s;
}

static void update(const DomeFrame *f) { g_t += f->dt * L.speed; }

static void draw(const DomeFrame *f)
{
    int s = style();
    GLuint p = g_prog[s];
    dome_surface_begin(p, f);
    glUniform1f(glGetUniformLocation(p, "u_t"), g_t);
    glUniform1f(glGetUniformLocation(p, "u_scale"), L.scale);
    glUniform1f(glGetUniformLocation(p, "u_bright"), L.bright);
    glUniform1f(glGetUniformLocation(p, "u_theme"), floorf(L.theme + 0.5f));
    dome_surface_end();

    mat3 still; m3_identity(still);
    float dim = f->fade * fminf(L.bright, 1.0f);
    if (s == 0) {
        dome_points_clear(&g_lines);
        tesseract(g_t);
        dome_points_upload(&g_lines);
        dome_draw_points(&g_lines, still, dim, 3.0f);
    } else if (s == 7) {
        dome_points_clear(&g_lines);
        pendulum(g_t);
        dome_points_upload(&g_lines);
        dome_points_upload(&g_verts);
        dome_draw_points(&g_lines, f->rot, dim, 3.0f);
        dome_draw_points(&g_verts, f->rot, dim, 16.0f);
    } else if (s == 9) {
        mat3 a, b, ra, rb;
        m3_ry(a, g_t * 0.10f);
        m3_ry(b, -g_t * 0.17f);
        m3_mul(ra, f->rot, a);
        m3_mul(rb, f->rot, b);
        dome_draw_points(&g_geo_in, rb, dim * 0.9f, 3.0f);
        dome_draw_points(&g_geo_out, ra, dim * (0.75f + 0.25f * sinf(g_t * 1.3f)), 3.0f);
    }
}

static int key(SDL_Keycode k, int shift)
{
    (void)shift;
    if (k == SDLK_n) {
        L.style = (float)((style() + 1) % NSTYLE);
        printf("Hypnotic: %s\n", NAMES[style()]);
        return 1;
    }
    if (k == SDLK_m) {
        L.theme = (float)(((int)lroundf(L.theme) + 1) % (DOME_THEMES + 1));
        int t = (int)L.theme;
        printf("Hypnotic: %s colours\n", t == 0 ? "classic" : DOME_THEME_NAMES[t - 1]);
        return 1;
    }
    return 0;
}

Scene scene_hypno = {
    "HYPNOTIC", "hypno.cfg", 0, P, (int)(sizeof P / sizeof P[0]),
    init, NULL, update, draw, key,
    "N next style, M colours", NULL
};
