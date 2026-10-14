/*
 * groovy.c  -  60s and 70s patterns. N steps through the styles:
 *
 *   0 op art         black and white rings that ripple and interfere
 *   1 psychedelic    bold rainbow bands spiralling round, poster style
 *   2 flower power   pop-colour daisies opening and closing in waves
 *   3 lava lamp      glowing wax blobs rising, merging and splitting
 *   4 liquid light   swirling coloured oils, like a 60s light show
 *   5 tie-dye        spiral dye bands bleeding outwards from a fold,
 *                    then a fresh dye spreading over the old one
 *   6 paisley        curling paisley teardrops drifting over the globe
 *   7 spirograph     a pen tracing gear patterns out to the rim, layer
 *                    on layer, then a clean sheet
 *
 * The patterns on the ball turn with it; the lava lamp and spirograph
 * stay put, facing you. groovy.cfg from before 2026-10 used other style
 * numbers; an old value just picks a different style.
 */
#include "../dome/dome.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define NSTYLE 8
static const char *NAMES[NSTYLE] = {
    "op art", "psychedelic swirl", "flower power", "lava lamp",
    "liquid light", "tie-dye", "paisley", "spirograph"
};

static struct { float style, speed, scale, bright; } L = { 0.0f, 1.0f, 1.0f, 1.0f };

static Param P[] = {
    { "style",  &L.style,  1.0f, 0.0f, 7.0f, 0,
      "0 op art, 1 swirl, 2 flowers, 3 lava lamp, 4 liquid light, 5 tie-dye, 6 paisley, 7 spirograph (N)" },
    { "speed",  &L.speed,  0.1f, 0.0f, 4.0f, 0, "speed of the animation" },
    { "scale",  &L.scale,  0.05f, 0.4f, 2.5f, 0, "size of the pattern (bigger = finer)" },
    { "bright", &L.bright, 0.05f, 0.2f, 2.0f, 0, "brightness" },
};

#define U "uniform float u_t, u_scale, u_bright;\n"

/* ---- 0: op art ---- */
static const char OPART[] =
    U
    "vec4 surface(vec3 dir, vec3 view){\n"
    /* two sets of rings round two slowly wandering centres */
    "  vec3 A = normalize(vec3(sin(u_t * 0.13), cos(u_t * 0.11), 0.6));\n"
    "  vec3 B = normalize(vec3(-0.7, sin(u_t * 0.09 + 1.0), cos(u_t * 0.07)));\n"
    "  float a1 = acos(clamp(dot(dir, A), -1.0, 1.0));\n"
    "  float a2 = acos(clamp(dot(dir, B), -1.0, 1.0));\n"
    "  float k = 18.0 * u_scale;\n"
    "  float w = sin(a1 * k + sin(a2 * 3.0 + u_t) * 2.0 - u_t * 1.5);\n"
    "  float v = sin(a2 * k * 0.8 - u_t);\n"
    "  return vec4(vec3(smoothstep(-0.12, 0.12, w * v)) * u_bright, 1.0);\n"
    "}\n";

/* ---- 1: psychedelic swirl: bold bands spiralling round two poles, with
 * black outlines like a 60s poster. Only a few sums per pixel. ---- */
static const char SWIRL[] =
    U
    "vec3 pop(float i){\n"
    "  i = mod(i, 6.0);\n"
    "  if (i < 1.0) return vec3(1.00, 0.15, 0.55);\n"     /* hot pink */
    "  if (i < 2.0) return vec3(1.00, 0.55, 0.00);\n"     /* orange   */
    "  if (i < 3.0) return vec3(1.00, 0.90, 0.10);\n"     /* yellow   */
    "  if (i < 4.0) return vec3(0.20, 0.85, 0.30);\n"     /* green    */
    "  if (i < 5.0) return vec3(0.10, 0.75, 0.95);\n"     /* cyan     */
    "  return vec3(0.55, 0.20, 0.90);\n"                  /* violet   */
    "}\n"
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  float lat = asin(clamp(dir.y, -1.0, 1.0)), lon = atan(dir.x, dir.z);\n"
    /* spiral: the bands wind with latitude and wobble as they turn */
    "  float v = lon * 2.0 + lat * 9.0 * u_scale + sin(lat * 4.0 + u_t * 0.6) * 1.2 - u_t * 0.8;\n"
    "  float b = v / 1.0472;\n"                           /* one band per 60 degrees */
    "  float f = fract(b);\n"
    "  vec3 col = pop(floor(b));\n"
    "  float edge = min(f, 1.0 - f);\n"
    "  col *= smoothstep(0.02, 0.07, edge);\n"             /* black outline */
    "  col *= 0.85 + 0.15 * cos(f * 6.2832);\n"            /* slight sheen */
    "  return vec4(col * u_bright, 1.0);\n"
    "}\n";

/* ---- 4: liquid light, rebuilt from sine waves instead of noise so it is
 * cheap enough for the Pi 3. The oils are layers of waves, each one bent
 * by the one before (domain warping), which makes them swirl and fold. ---- */
static const char LIQUID[] =
    U
    "vec3 wv(vec3 p, float t){\n"
    "  return vec3(sin(p.y * 2.3 + t) + sin(p.z * 1.7 - t * 0.7),\n"
    "              sin(p.z * 2.1 - t * 0.8) + sin(p.x * 1.9 + t * 0.6),\n"
    "              sin(p.x * 2.5 + t * 0.9) + sin(p.y * 1.6 - t * 0.5));\n"
    "}\n"
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  float t = u_t * 0.35;\n"
    "  vec3 p = dir * 1.8 * u_scale;\n"
    "  p += wv(p, t) * 0.45;\n"
    "  p += wv(p * 1.3, -t * 1.2) * 0.30;\n"
    "  float f = 0.5 + 0.25 * (sin(p.x * 1.6 + p.y) + sin(p.z * 1.8 - p.x * 0.7));\n"
    "  float g = 0.5 + 0.25 * (sin(p.y * 2.2 - p.z) + sin(p.x * 2.6 + t));\n"
    "  vec3 col = mix(vec3(0.45, 0.10, 0.90), vec3(1.0, 0.15, 0.55), smoothstep(0.35, 0.55, f));\n"
    "  col = mix(col, vec3(1.0, 0.75, 0.05), smoothstep(0.62, 0.72, f));\n"
    "  col = mix(col, vec3(0.10, 0.85, 0.80), smoothstep(0.50, 0.62, g) * 0.8);\n"
    "  col = mix(col, vec3(0.20, 0.90, 0.20), smoothstep(0.70, 0.80, g) * 0.6);\n"
    "  col += vec3(1.0) * smoothstep(0.025, 0.0, abs(f - 0.55)) * 0.35;\n"   /* oily edges */
    "  return vec4(col * u_bright, 1.0);\n"
    "}\n";

/* ---- 2: flower power ---- */
static const char FLOWERS[] =
    U
    "vec3 petal(float h){\n"
    "  if (h < 0.2) return vec3(1.0, 0.30, 0.60);\n"
    "  if (h < 0.4) return vec3(1.0, 0.55, 0.10);\n"
    "  if (h < 0.6) return vec3(0.60, 0.90, 0.10);\n"
    "  if (h < 0.8) return vec3(0.60, 0.30, 0.90);\n"
    "  return vec3(1.0, 1.0, 0.95);\n"
    "}\n"
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  float lat = asin(clamp(dir.y, -1.0, 1.0)), lon = atan(dir.x, dir.z);\n"
    "  float N = floor(9.0 * u_scale + 0.5);\n"
    "  vec2 g = vec2(lon / (2.0 * PI) * N, lat / PI * N);\n"
    "  vec2 cell = floor(g), f = fract(g) - 0.5;\n"
    "  float h = h3(vec3(cell, 1.0));\n"
    "  float bloom = 0.72 + 0.28 * sin(u_t * 1.3 - (cell.x + cell.y) * 0.6);\n"
    "  float d = length(f), ang = atan(f.y, f.x);\n"
    "  float pr = (0.20 + 0.22 * pow(abs(cos(ang * 3.0 + h * 6.0)), 0.6)) * bloom;\n"
    "  vec3 c = mod(cell.x + cell.y, 2.0) < 0.5 ? vec3(0.10, 0.72, 0.72) : vec3(0.98, 0.82, 0.20);\n"
    "  if (d < pr) c = petal(h) * (0.85 + 0.15 * smoothstep(pr, pr * 0.5, d));\n"
    "  c *= 1.0 - 0.5 * smoothstep(0.03, 0.0, abs(d - pr));\n"          /* outline */
    "  float cr = 0.10 * bloom;\n"
    "  if (d < cr) c = mix(vec3(1.0, 0.6, 0.05), vec3(1.0, 0.85, 0.2), d / cr);\n"
    "  return vec4(c * u_bright, 1.0);\n"
    "}\n";

/* ---- 3: lava lamp (fixed to the dome) ---- */
static const char LAVA[] =
    U
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  float F = 0.0;\n"
    "  for (int i = 0; i < 7; i++) {\n"
    "    float fi = float(i);\n"
    "    vec3 c = normalize(vec3(sin(fi * 1.7 + u_t * 0.07 * (1.0 + fi * 0.13)) * 0.7,\n"
    "                            sin(u_t * (0.05 + fi * 0.017) + fi * 2.1) * 0.95,\n"
    "                            0.6 + 0.4 * cos(fi * 2.3 + u_t * 0.03)));\n"
    "    float r = (0.25 + 0.08 * sin(fi * 3.1)) / u_scale;\n"
    "    vec3 d = view - c;\n"
    "    F += r * r / max(dot(d, d), 1e-4);\n"
    "  }\n"
    "  vec3 col = mix(vec3(0.20, 0.02, 0.14), vec3(0.50, 0.05, 0.12), view.y * 0.5 + 0.5) * 0.7;\n"
    "  col += vec3(1.0, 0.30, 0.05) * smoothstep(0.4, 1.0, F) * 0.25;\n"      /* glow */
    "  vec3 wax = mix(vec3(1.0, 0.32, 0.04), vec3(1.0, 0.80, 0.25), smoothstep(1.0, 3.5, F));\n"
    "  col = mix(col, wax, smoothstep(0.92, 1.08, F));\n"
    "  return vec4(col * u_bright, 1.0);\n"
    "}\n";

/* ---- 5: tie-dye. Five dyes wound into a spiral round a fold point,
 * crinkled where the cloth was bunched, with pale bleed lines between
 * the bands. Every 40 seconds a new fold point is dyed and its colours
 * spread out over the old ones until they cover the globe. ---- */
static const char TIEDYE[] =
    U
    "vec3 dye(float i){\n"
    "  i = mod(i, 5.0);\n"
    "  if (i < 1.0) return vec3(0.95, 0.08, 0.45);\n"     /* magenta */
    "  if (i < 2.0) return vec3(1.00, 0.50, 0.05);\n"     /* orange  */
    "  if (i < 3.0) return vec3(1.00, 0.90, 0.12);\n"     /* yellow  */
    "  if (i < 4.0) return vec3(0.08, 0.70, 0.95);\n"     /* sky     */
    "  return vec3(0.42, 0.12, 0.85);\n"                  /* purple  */
    "}\n"
    "vec3 fold(float k){\n"                               /* fold point of dye cycle k */
    "  return normalize(vec3(sin(k * 2.4), 0.6 * sin(k * 1.7 + 1.0), cos(k * 2.4)));\n"
    "}\n"
    "vec3 pattern(vec3 dir, float k, out float a){\n"
    "  vec3 C = fold(k);\n"
    "  a = acos(clamp(dot(dir, C), -1.0, 1.0));\n"
    "  vec3 e1 = normalize(cross(C, vec3(0.31, 0.95, 0.0)));\n"
    "  vec3 e2 = cross(C, e1);\n"
    "  float th = atan(dot(dir, e2), dot(dir, e1));\n"
    "  float crk = sin(th * 7.0 + a * 5.0) * 0.18 + sin(th * 19.0 - a * 13.0) * 0.07;\n"
    "  float v = a * 3.2 * u_scale + th * 5.0 / (2.0 * PI) + crk - u_t * 0.05;\n"
    "  float f = fract(v), i = floor(v) + k * 2.0;\n"
    "  vec3 col = mix(dye(i), dye(i + 1.0), smoothstep(0.80, 1.0, f));\n"
    "  col = mix(col, vec3(1.0, 0.97, 0.92), 0.55 * smoothstep(0.07, 0.0, min(f, 1.0 - f)));\n"
    "  return col * (0.88 + 0.12 * sin(th * 40.0 + a * 30.0));\n"   /* weave */
    "}\n"
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  float T = u_t / 40.0, k = floor(T), ph = fract(T);\n"
    "  float a0, a1;\n"
    "  vec3 old = pattern(dir, k - 1.0, a0);\n"
    "  vec3 fresh = pattern(dir, k, a1);\n"
    "  float R = ph * 4.2;\n"                            /* the new dye front */
    "  float wet = smoothstep(R + 0.25, R - 0.05, a1);\n"
    "  vec3 col = mix(old, fresh, wet);\n"
    "  col *= 1.0 - 0.25 * exp(-pow((a1 - R) * 9.0, 2.0)) * step(ph, 0.85);\n"   /* dark wet edge */
    "  return vec4(col * u_bright, 1.0);\n"
    "}\n";

/* ---- 6: paisley: rows of teardrops round the globe, each with a curled
 * tail, outlined bands, an eye and a ring of dots, swaying as the rows
 * drift in opposite directions ---- */
static const char PAISLEY[] =
    U
    "vec3 ink(float i){\n"
    "  i = mod(i, 4.0);\n"
    "  if (i < 1.0) return vec3(0.95, 0.70, 0.10);\n"     /* mustard */
    "  if (i < 2.0) return vec3(0.85, 0.15, 0.20);\n"     /* red     */
    "  if (i < 3.0) return vec3(0.10, 0.65, 0.60);\n"     /* teal    */
    "  return vec3(0.85, 0.30, 0.65);\n"                  /* rose    */
    "}\n"
    "vec2 rot2(vec2 p, float a){ float c = cos(a), s = sin(a); return vec2(c * p.x - s * p.y, s * p.x + c * p.y); }\n"
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  float lat = asin(clamp(dir.y, -1.0, 1.0)), lon = atan(dir.x, dir.z);\n"
    "  float rows = floor(7.0 * u_scale + 0.5);\n"
    "  float rf = (lat / PI + 0.5) * rows, row = floor(rf);\n"
    "  float latc = ((row + 0.5) / rows - 0.5) * PI;\n"
    "  float n = max(2.0, floor(rows * 2.0 * cos(latc) + 0.5));\n"
    "  float drift = u_t * 0.012 * (mod(row, 2.0) < 0.5 ? 1.0 : -1.0);\n"
    "  float cf = (lon / (2.0 * PI) + 0.5 + drift) * n, cl = floor(cf);\n"
    "  cl = mod(cl, n);\n"
    "  vec2 q = vec2((fract(cf) - 0.5) * 2.0 * rows * cos(lat) / n, fract(rf) - 0.5);\n"
    "  float h = h3(vec3(row, cl, 2.0));\n"
    "  float flip = mod(row + cl, 2.0) < 0.5 ? 1.0 : -1.0;\n"
    "  vec3 bg = mod(row, 2.0) < 0.5 ? vec3(0.10, 0.04, 0.16) : vec3(0.04, 0.10, 0.14);\n"
    "  vec3 col = bg + vec3(0.05, 0.03, 0.02) * step(0.8, fract((q.x + q.y) * 18.0));\n"
    /* the teardrop, bent so its tip curls */
    "  vec2 p = rot2(q * 0.70, flip * 0.5 + 0.25 * sin(u_t * 0.5 + h * 6.0));\n"
    "  p.x *= flip;\n"
    "  vec2 b = vec2(p.x - 1.6 * pow(max(p.y + 0.02, 0.0), 2.0), p.y + 0.06);\n"
    "  float w = b.y < 0.0 ? 0.27 * sqrt(max(0.0, 1.0 - pow(b.y / 0.27, 2.0)))\n"
    "                      : 0.27 * pow(max(0.0, 1.0 - b.y / 0.44), 1.4);\n"
    "  float d = abs(b.x) - w;\n"
    "  if (b.y < -0.27 || b.y > 0.44) d = max(d, 0.02);\n"
    "  float ci = floor(h * 4.0);\n"
    "  if (d < 0.0) {\n"
    "    float band = floor(-d / 0.045);\n"
    "    col = ink(ci + band) * (0.75 + 0.25 * smoothstep(0.0, 0.02, fract(-d / 0.045) * 0.045));\n"
    "    float e = length(b - vec2(0.0, -0.06));\n"         /* the eye */
    "    if (e < 0.08) col = mix(ink(ci + 2.0), vec3(1.0, 0.95, 0.8), step(e, 0.03));\n"
    "    col *= smoothstep(0.0, 0.012, abs(e - 0.08));\n"
    "  }\n"
    "  col *= smoothstep(0.0, 0.012, abs(d));\n"           /* outline */
    "  float dots = step(0.03, d) * step(d, 0.075) * step(0.55, fract(atan(b.y + 0.06, b.x) * 4.0));\n"
    "  col = mix(col, ink(ci + 1.0) * 1.1, dots * step(length(fract(vec2(atan(b.y + 0.06, b.x) * 4.0, d * 18.0)) - 0.5), 0.3));\n"
    "  return vec4(col * u_bright, 1.0);\n"
    "}\n";

/* ---- 7: spirograph: a dark sheet (surface), with the pen lines drawn as
 * dots in dome space. The fixed ring and the rolling gear are shown
 * faintly while the pen is drawing. ---- */
static const char SHEET[] =
    U
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  float a = acos(clamp(view.z, -1.0, 1.0));\n"
    "  vec3 col = mix(vec3(0.05, 0.06, 0.13), vec3(0.01, 0.01, 0.04), a / 1.6);\n"
    "  col += vec3(0.03) * (h3(floor(view * 300.0)) - 0.5);\n"   /* paper grain */
    "  return vec4(col * u_bright, 1.0);\n"
    "}\n";

static const char *GLSL[NSTYLE] = { OPART, SWIRL, FLOWERS, LAVA, LIQUID, TIEDYE, PAISLEY, SHEET };
static GLuint g_prog[NSTYLE];
static float  g_t;

/* ---------------------------------------------------------------- */
/* Spirograph                                                         */
/* ---------------------------------------------------------------- */
#define SPIRO_LAYERS 4
#define SPIRO_STEPS  5000          /* dots per curve */
#define RIM_DEG      88.0f

static DomePoints g_done, g_pen;   /* finished curves; the one being drawn */
static struct { float R, r, d, col[3], turn; } g_curve[SPIRO_LAYERS];
static int   g_layer;              /* curve being drawn */
static float g_prog_t;             /* 0..1 along it */
static float g_fade = 1.0f;        /* 1, or fading out after the last layer */
static int   g_set;

static const float PENS[6][3] = {
    { 1.0f, 0.35f, 0.70f }, { 0.25f, 0.85f, 1.0f }, { 1.0f, 0.85f, 0.25f },
    { 0.45f, 1.0f, 0.45f }, { 1.0f, 0.55f, 0.20f }, { 0.75f, 0.50f, 1.0f },
};

static int gcd(int a, int b) { while (b) { int t = a % b; a = b; b = t; } return a; }

/* a point in the flat drawing (radius 1 = the rim) onto the dome */
static void flat_to_dome(float x, float y, float out[3])
{
    float rho = sqrtf(x * x + y * y);
    float a = rho * DEG2RAD(RIM_DEG);
    float s = rho > 1e-6f ? sinf(a) / rho : 0.0f;
    out[0] = x * s; out[1] = y * s; out[2] = cosf(a);
}

/* the pen's position at parameter t (radians) on curve c, flat coords */
static void spiro_at(int c, float t, float *x, float *y)
{
    float R = g_curve[c].R, r = g_curve[c].r, d = g_curve[c].d;
    float k = (R - r) / r, norm = 1.0f / (R - r + d);
    float px = (R - r) * cosf(t) + d * cosf(k * t);
    float py = (R - r) * sinf(t) - d * sinf(k * t);
    float c0 = cosf(g_curve[c].turn), s0 = sinf(g_curve[c].turn);
    *x = (px * c0 - py * s0) * norm;
    *y = (px * s0 + py * c0) * norm;
}

static float spiro_len(int c)      /* parameter length of the closed curve */
{
    int R = (int)g_curve[c].R, r = (int)g_curve[c].r;
    return 2.0f * DOME_PI * (float)(r / gcd(R, r));
}

static void spiro_push(DomePoints *p, int c, float t0, float t1, int steps, float a)
{
    float v[3];
    for (int i = 0; i <= steps; i++) {
        float x, y;
        spiro_at(c, t0 + (t1 - t0) * i / steps, &x, &y);
        flat_to_dome(x, y, v);
        dome_points_push(p, v[0], v[1], v[2], g_curve[c].col[0], g_curve[c].col[1], g_curve[c].col[2], a);
    }
}

static void spiro_new_set(void)
{
    static const int RING[3] = { 96, 105, 144 };
    static const int GEAR[10] = { 24, 30, 32, 40, 45, 52, 56, 60, 63, 84 };
    int R = RING[g_set % 3];
    int first = rand() % 6;
    for (int c = 0; c < SPIRO_LAYERS; c++) {
        int r;
        do r = GEAR[rand() % 10]; while (r >= R || R / gcd(R, r) > 16 || r / gcd(R, r) > 14);
        g_curve[c].R = (float)R;
        g_curve[c].r = (float)r;
        g_curve[c].d = r * (0.55f + 0.4f * (float)rand() / RAND_MAX);
        g_curve[c].turn = c * 0.4f;
        for (int k = 0; k < 3; k++) g_curve[c].col[k] = PENS[(first + c) % 6][k];
    }
    g_set++;
    g_layer = 0; g_prog_t = 0; g_fade = 1.0f;
    dome_points_clear(&g_done);
    dome_points_upload(&g_done);
}

static void spiro_update(float dt)
{
    if (g_layer >= SPIRO_LAYERS) {             /* admire it, then fade */
        g_fade -= dt / 4.0f;
        if (g_fade < -0.5f) spiro_new_set();
        return;
    }
    g_prog_t += dt / 14.0f;                    /* 14 s per curve */
    if (g_prog_t >= 1.0f) {
        int c = g_layer;
        spiro_push(&g_done, c, 0.0f, spiro_len(c), SPIRO_STEPS, 0.9f);
        dome_points_upload(&g_done);
        g_layer++; g_prog_t = 0;
        if (g_layer >= SPIRO_LAYERS) g_fade = 2.0f;   /* hold, then fade */
    }
}

static void spiro_draw(const DomeFrame *f)
{
    mat3 spin; m3_rz(spin, g_t * 0.03f);
    float a = f->fade * fminf(1.0f, fmaxf(g_fade, 0.0f));
    dome_draw_points(&g_done, spin, a, 3.0f);
    dome_points_clear(&g_pen);
    if (g_layer < SPIRO_LAYERS) {
        int c = g_layer;
        float tl = spiro_len(c) * g_prog_t;
        spiro_push(&g_pen, c, 0.0f, tl, (int)(SPIRO_STEPS * g_prog_t) + 1, 0.9f);
        /* the toy: fixed ring and rolling gear, faint */
        float R = g_curve[c].R, r = g_curve[c].r, norm = 1.0f / (R - r + g_curve[c].d);
        float c0 = cosf(g_curve[c].turn), s0 = sinf(g_curve[c].turn);
        float gx = (R - r) * cosf(tl), gy = (R - r) * sinf(tl), v[3];
        for (int i = 0; i < 360; i++) {
            float ang = i * DOME_PI / 180.0f;
            float x = R * cosf(ang) * norm, y = R * sinf(ang) * norm;
            if (x * x + y * y < 1.0f) {
                flat_to_dome(x, y, v);
                dome_points_push(&g_pen, v[0], v[1], v[2], 0.6f, 0.6f, 0.7f, 0.25f);
            }
            if (i % 2) continue;
            float hx = gx + r * cosf(ang), hy = gy + r * sinf(ang);
            x = (hx * c0 - hy * s0) * norm; y = (hx * s0 + hy * c0) * norm;
            if (x * x + y * y < 1.0f) {
                flat_to_dome(x, y, v);
                dome_points_push(&g_pen, v[0], v[1], v[2], 0.7f, 0.7f, 0.8f, 0.30f);
            }
        }
        float px, py;                              /* the pen */
        spiro_at(c, tl, &px, &py);
        flat_to_dome(px, py, v);
        dome_points_push(&g_pen, v[0], v[1], v[2], 1.0f, 1.0f, 1.0f, 1.0f);
    }
    dome_points_upload(&g_pen);
    dome_draw_points(&g_pen, spin, f->fade, 3.0f);
}

/* ---------------------------------------------------------------- */
static void init(void)
{
    for (int i = 0; i < NSTYLE; i++) g_prog[i] = dome_surface_program_lib(DOME_GLSL_NOISE, GLSL[i]);
    dome_points_init(&g_done, SPIRO_LAYERS * (SPIRO_STEPS + 1) + 16);
    dome_points_init(&g_pen, SPIRO_STEPS + 1000);
    spiro_new_set();
}

static int style(void)
{
    int s = (int)lroundf(L.style);
    return s < 0 ? 0 : s >= NSTYLE ? NSTYLE - 1 : s;
}

static void update(const DomeFrame *f)
{
    float dt = f->dt * L.speed;
    g_t += dt;
    if (style() == 7) spiro_update(dt);
}

static void draw(const DomeFrame *f)
{
    int s = style();
    GLuint p = g_prog[s];
    dome_surface_begin(p, f);
    glUniform1f(glGetUniformLocation(p, "u_t"), g_t);
    glUniform1f(glGetUniformLocation(p, "u_scale"), L.scale);
    glUniform1f(glGetUniformLocation(p, "u_bright"), L.bright);
    dome_surface_end();
    if (s == 7) spiro_draw(f);
}

static int key(SDL_Keycode k, int shift)
{
    (void)shift;
    if (k != SDLK_n) return 0;
    L.style = (float)((style() + 1) % NSTYLE);
    printf("Groovy: %s\n", NAMES[style()]);
    return 1;
}

Scene scene_groovy = {
    "GROOVY", "groovy.cfg", 0, P, (int)(sizeof P / sizeof P[0]),
    init, NULL, update, draw, key,
    "N next style", NULL
};
