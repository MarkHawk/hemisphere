/*
 * groovy.c  -  60s and 70s patterns. N steps through the styles:
 *
 *   0 op art         black and white rings that ripple and interfere
 *   1 psychedelic    bold rainbow bands spiralling round, poster style
 *   2 70s wallpaper  interlocking circles and wavy stripes, scrolling
 *   3 flower power   pop-colour daisies opening and closing in waves
 *   4 lava lamp      glowing wax blobs rising, merging and splitting
 *   5 spirograph     looping patterns drawn line by line on cream paper
 *   6 mirror ball    a disco ball of glinting tiles under coloured lights
 *   7 liquid light   swirling coloured oils, like a 60s light show
 *
 * The patterns on the ball turn with it; the lava lamp and spirograph
 * stay put, facing you.
 */
#include "../dome/dome.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define NSTYLE 8
static const char *NAMES[NSTYLE] = {
    "op art", "psychedelic swirl", "70s wallpaper", "flower power",
    "lava lamp", "spirograph", "mirror ball", "liquid light"
};

static struct { float style, speed, scale, bright; } L = { 0.0f, 1.0f, 1.0f, 1.0f };

static Param P[] = {
    { "style",  &L.style,  1.0f, 0.0f, 7.0f, 0,
      "0 op art, 1 swirl, 2 wallpaper, 3 flowers, 4 lava lamp, 5 spirograph, 6 mirror ball, 7 liquid light (N)" },
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

/* ---- 7: liquid light, rebuilt from sine waves instead of noise so it is
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

/* ---- 2: 70s wallpaper ---- */
static const char WALLPAPER[] =
    U
    "vec3 c5(float i){\n"
    "  if (i < 0.5) return vec3(0.36, 0.20, 0.10);\n"       /* brown    */
    "  if (i < 1.5) return vec3(0.86, 0.45, 0.10);\n"       /* orange   */
    "  if (i < 2.5) return vec3(0.90, 0.70, 0.20);\n"       /* mustard  */
    "  if (i < 3.5) return vec3(0.50, 0.55, 0.20);\n"       /* avocado  */
    "  return vec3(0.95, 0.88, 0.70);\n"                    /* cream    */
    "}\n"
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  float lat = asin(clamp(dir.y, -1.0, 1.0)), lon = atan(dir.x, dir.z);\n"
    "  float N = floor(10.0 * u_scale + 0.5);\n"
    "  vec2 g = vec2(lon / (2.0 * PI) * N, lat / PI * N + u_t * 0.05);\n"
    "  vec2 cell = floor(g);\n"
    "  vec2 f = fract(g) - 0.5;\n"
    "  if (mod(cell.y, 2.0) > 0.5) { f.x = fract(g.x + 0.5) - 0.5; cell.x = floor(g.x + 0.5); }\n"
    "  float d = length(f);\n"
    "  vec3 c;\n"
    "  if (d < 0.47) {\n"
    "    float band = floor(d * 8.0);\n"
    "    c = c5(mod(band + mod(cell.x + cell.y, 2.0) * 2.0, 5.0));\n"
    "    c *= 0.85 + 0.15 * smoothstep(0.0, 0.08, abs(fract(d * 8.0) - 0.5));\n"
    "  } else {\n"
    "    float s = fract((g.y + 0.08 * sin(g.x * 12.566)) * 5.0);\n"
    "    c = s < 0.5 ? c5(4.0) : c5(1.0) * 0.9;\n"
    "  }\n"
    "  return vec4(c * u_bright, 1.0);\n"
    "}\n";

/* ---- 3: flower power ---- */
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

/* ---- 4: lava lamp (fixed to the dome) ---- */
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

/* ---- 5: spirograph paper (the lines are dots, drawn on top) ---- */
static const char PAPER[] =
    U
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  float a = acos(clamp(view.z, -1.0, 1.0));\n"
    "  vec3 c = vec3(0.96, 0.92, 0.82) * (0.94 + 0.06 * n3(view * 60.0));\n"
    "  c *= 1.0 - 0.25 * smoothstep(1.0, 1.55, a);\n"
    "  return vec4(c * u_bright, 1.0);\n"
    "}\n";

/* ---- 6: mirror ball ---- */
static const char MIRROR[] =
    U
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  float lat = asin(clamp(dir.y, -1.0, 1.0)), lon = atan(dir.x, dir.z);\n"
    "  float rows = floor(36.0 * u_scale + 0.5);\n"
    "  float rf = (lat / PI + 0.5) * rows;\n"
    "  float row = floor(rf);\n"
    "  float latc = ((row + 0.5) / rows - 0.5) * PI;\n"
    "  float n = max(4.0, floor(rows * 2.0 * cos(latc) + 0.5));\n"
    "  float cf = (lon / (2.0 * PI) + 0.5) * n;\n"
    "  float col_i = floor(cf);\n"
    "  vec2 fr = vec2(fract(cf), fract(rf));\n"
    "  float grout = smoothstep(0.0, 0.10, min(min(fr.x, 1.0 - fr.x), min(fr.y, 1.0 - fr.y)));\n"
    "  float h = h3(vec3(row, col_i, 3.0));\n"
    /* each tile is a flat mirror, tilted a touch */
    "  vec3 nt = normalize(dir + (vec3(h3(vec3(row, col_i, 1.0)), h3(vec3(row, col_i, 2.0)), h) - 0.5) * 0.08);\n"
    "  vec3 nv = u_rot * nt;\n"
    "  vec3 R = reflect(vec3(0.0, 0.0, -1.0), nv);\n"
    "  vec3 spec = vec3(0.0);\n"
    "  for (int k = 0; k < 4; k++) {\n"
    "    float fk = float(k);\n"
    "    vec3 Lk = normalize(vec3(sin(u_t * 0.3 + fk * 1.57), 0.5 * cos(u_t * 0.23 + fk * 2.1), 0.8));\n"
    "    vec3 ck = fk < 0.5 ? vec3(1.0, 0.3, 0.8) : fk < 1.5 ? vec3(0.3, 0.9, 1.0)\n"
    "            : fk < 2.5 ? vec3(1.0, 0.85, 0.3) : vec3(1.0);\n"
    "    spec += ck * (pow(max(dot(R, Lk), 0.0), 40.0) * 1.3 + pow(max(dot(R, Lk), 0.0), 6.0) * 0.12);\n"
    "  }\n"
    "  vec3 base = vec3(0.40, 0.42, 0.46) * (0.55 + 0.6 * h) * (0.45 + 0.55 * max(nv.z, 0.0));\n"
    "  float spark = step(0.996, h3(vec3(row, col_i, floor(u_t * 6.0))));\n"
    "  vec3 c = (base + spec + vec3(spark)) * grout;\n"
    "  return vec4(c * u_bright, 1.0);\n"
    "}\n";

static const char *GLSL[NSTYLE] = { OPART, SWIRL, WALLPAPER, FLOWERS, LAVA, PAPER, MIRROR, LIQUID };
static GLuint g_prog[NSTYLE];
static float  g_t;

/* ---------------------------------------------------------------- */
/* Spirograph                                                        */
/* ---------------------------------------------------------------- */
#define NCURVE 3
typedef struct { int R, r; float d, age, scale, rot; float col[3]; } Curve;
static Curve      g_cv[NCURVE];
static DomePoints g_ink;

static int gcd(int a, int b) { while (b) { int t = a % b; a = b; b = t; } return a; }

static void curve_new(Curve *c, float age)
{
    static const float INK[5][3] = {
        { 0.85f, 0.20f, 0.15f }, { 0.10f, 0.35f, 0.75f }, { 0.10f, 0.55f, 0.30f },
        { 0.90f, 0.50f, 0.05f }, { 0.45f, 0.20f, 0.60f },
    };
    do {
        c->R = 7 + rand() % 7;
        c->r = 2 + rand() % (c->R - 3);
    } while (gcd(c->R, c->r) == c->r);
    c->d = c->r * (0.4f + 0.9f * (float)rand() / RAND_MAX);
    c->age = age;
    float ext = (float)(c->R - c->r) + c->d;
    c->scale = (0.55f + 0.6f * (float)rand() / RAND_MAX) / ext;
    c->rot = (float)rand() / RAND_MAX * 6.2832f;
    const float *k = INK[rand() % 5];
    c->col[0] = k[0]; c->col[1] = k[1]; c->col[2] = k[2];
}

static void spiro_build(float dt)
{
    const float DRAW = 14.0f, HOLD = 5.0f, FADE = 3.0f;
    dome_points_clear(&g_ink);
    for (int i = 0; i < NCURVE; i++) {
        Curve *c = &g_cv[i];
        c->age += dt * L.speed;
        if (c->age > DRAW + HOLD + FADE) curve_new(c, 0.0f);
        float prog = fminf(c->age / DRAW, 1.0f);
        float alpha = c->age < DRAW + HOLD ? 1.0f : 1.0f - (c->age - DRAW - HOLD) / FADE;
        int turns = c->r / gcd(c->R, c->r);
        int n = (int)(1400 * turns * prog);
        float tmax = 6.2832f * turns * prog;
        float k = (float)(c->R - c->r) / c->r;
        for (int j = 0; j <= n; j++) {
            float t = tmax * j / (n > 0 ? n : 1);
            float x = (c->R - c->r) * cosf(t) + c->d * cosf(k * t);
            float y = (c->R - c->r) * sinf(t) - c->d * sinf(k * t);
            float cr = cosf(c->rot), sr = sinf(c->rot);
            float ex = (x * cr - y * sr) * c->scale, ey = (x * sr + y * cr) * c->scale;
            float a = sqrtf(ex * ex + ey * ey), th = atan2f(ey, ex), s = sinf(a);
            dome_points_push(&g_ink, s * cosf(th), s * sinf(th), cosf(a),
                             c->col[0], c->col[1], c->col[2], alpha * 0.9f);
        }
    }
    dome_points_upload(&g_ink);
}

/* ---------------------------------------------------------------- */
static void init(void)
{
    for (int i = 0; i < NSTYLE; i++) g_prog[i] = dome_surface_program_lib(DOME_GLSL_NOISE, GLSL[i]);
    dome_points_init(&g_ink, NCURVE * 1400 * 13 + 16);
    for (int i = 0; i < NCURVE; i++) curve_new(&g_cv[i], -6.0f * i);
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
    dome_surface_end();
    if (s == 5) {
        spiro_build(f->dt);
        mat3 still; m3_identity(still);
        dome_draw_points(&g_ink, still, f->fade, 3.0f);
    }
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
