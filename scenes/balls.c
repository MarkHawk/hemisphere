/*
 * balls.c  -  sports balls. The whole globe becomes the ball, lit from the
 * front and rolling. N steps through them:
 *
 *   0 football     12 black pentagons on 20 white hexagons, with seams
 *   1 basketball   pebbled orange with the classic black seams
 *   2 tennis ball  fuzzy yellow-green with the white curving seam
 *   3 cricket ball polished red leather, stitched seam round the middle
 *   4 baseball     white leather with red stitches along the figure-of-eight
 *   5 volleyball   18 panels in three colours
 *   6 golf ball    white, covered in dimples
 *   7 pool 8-ball  glossy black with the white 8 circle
 *   8 beach ball   six coloured gores with white caps
 *   9 bowling ball swirled resin with three finger holes
 *
 * The football is drawn from the real solid (a truncated icosahedron):
 * each point belongs to whichever face plane it reaches first. The tennis
 * and baseball seams follow the same curve, where height = x squared minus
 * y squared, which wraps round the ball in a figure of eight.
 *
 * roll tumbles the ball end over end on top of the usual spin.
 */
#include "../dome/dome.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NSTYLE 10
static const char *NAMES[NSTYLE] = {
    "football", "basketball", "tennis ball", "cricket ball", "baseball",
    "volleyball", "golf ball", "pool 8-ball", "beach ball", "bowling ball"
};

static struct { float style, roll, bright, shine; } L = { 0.0f, 0.3f, 1.0f, 1.0f };

static Param P[] = {
    { "style",  &L.style,  1.0f, 0.0f, 9.0f, 0,
      "0 football, 1 basketball, 2 tennis, 3 cricket, 4 baseball, 5 volleyball, "
      "6 golf, 7 pool 8-ball, 8 beach ball, 9 bowling ball (N)" },
    { "roll",   &L.roll,   0.05f, 0.0f, 3.0f, 0, "tumbling end over end, on top of the spin (0 = off)" },
    { "bright", &L.bright, 0.05f, 0.2f, 2.0f, 0, "brightness" },
    { "shine",  &L.shine,  0.1f, 0.0f, 2.0f, 0, "strength of the highlight" },
};

static const char LIB[] =
    "uniform float u_t, u_roll, u_bright, u_shine;\n"
    "const float PHI = 1.6180340;\n"
    /* the ball's own frame, tumbled end over end */
    "vec3 ball(vec3 d){ float c = cos(u_roll), s = sin(u_roll); return vec3(d.x, c * d.y - s * d.z, s * d.y + c * d.z); }\n"
    /* light from the upper left; gloss sets how sharp the highlight is */
    "vec3 lit(vec3 c, vec3 view, float gloss, float spec){\n"
    "  vec3 Lt = normalize(vec3(-0.45, 0.55, 0.70));\n"
    "  float dif = 0.30 + 0.70 * max(dot(view, Lt), 0.0);\n"
    "  vec3 R = reflect(-Lt, view);\n"
    "  float sp = pow(max(R.z, 0.0), gloss) * spec * u_shine;\n"
    "  return c * dif + vec3(sp);\n"
    "}\n"
    /* the figure-of-eight seam: signed distance, roughly in radians */
    "float seam8(vec3 d){\n"
    "  float f = d.z - 0.80 * (d.x * d.x - d.y * d.y);\n"
    "  vec3 g = vec3(-1.6 * d.x, 1.6 * d.y, 1.0);\n"
    "  g -= d * dot(g, d);\n"                                       /* along the surface */
    "  return f / max(length(g), 0.3);\n"
    "}\n";

/* ---- 0: football ---- */
static const char FOOTBALL[] =
    "void face(vec3 d, vec3 n, float w, float p, inout float b1, inout float b2, inout float isP){\n"
    "  float v = dot(d, normalize(n)) / w;\n"
    "  if (v > b1) { b2 = b1; b1 = v; isP = p; } else if (v > b2) b2 = v;\n"
    "}\n"
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  vec3 d = ball(dir);\n"
    "  float b1 = -9.0, b2 = -9.0, isP = 0.0;\n"
    "  const float WP = 2.32744, WH = 2.26728;\n"                   /* face distances */
    "  for (int i = 0; i < 4; i++) {\n"
    "    float s1 = mod(float(i), 2.0) < 0.5 ? 1.0 : -1.0, s2 = i < 2 ? 1.0 : -1.0;\n"
    "    face(d, vec3(0.0, s1, s2 * PHI), WP, 1.0, b1, b2, isP);\n"
    "    face(d, vec3(s1, s2 * PHI, 0.0), WP, 1.0, b1, b2, isP);\n"
    "    face(d, vec3(s2 * PHI, 0.0, s1), WP, 1.0, b1, b2, isP);\n"
    "    face(d, vec3(0.0, s1 * PHI, s2 / PHI), WH, 0.0, b1, b2, isP);\n"    /* hexagons sit on the */
    "    face(d, vec3(s1 * PHI, s2 / PHI, 0.0), WH, 0.0, b1, b2, isP);\n"    /* icosahedron's faces */
    "    face(d, vec3(s2 / PHI, 0.0, s1 * PHI), WH, 0.0, b1, b2, isP);\n"
    "    face(d, vec3(s1, s2, 1.0), WH, 0.0, b1, b2, isP);\n"
    "    face(d, vec3(s1, s2, -1.0), WH, 0.0, b1, b2, isP);\n"
    "  }\n"
    "  vec3 c = isP > 0.5 ? vec3(0.06) : vec3(0.94);\n"
    "  float e = (b1 - b2) / b1;\n"
    "  c *= 0.35 + 0.65 * smoothstep(0.002, 0.008, e);\n"           /* stitched seams */
    "  return vec4(lit(c, view, 30.0, 0.35) * u_bright, 1.0);\n"
    "}\n";

/* ---- 1: basketball ---- */
static const char BASKETBALL[] =
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  vec3 d = ball(dir);\n"
    "  vec3 c = vec3(0.85, 0.36, 0.10);\n"
    "  vec3 g = floor(d * 110.0);\n"                                /* pebbles */
    "  c *= 0.86 + 0.14 * h3(g);\n"
    "  float w = 0.012;\n"
    "  float s = min(abs(d.x), abs(d.y));\n"                        /* two great circles */
    "  s = min(s, abs(abs(d.z) - 0.62 - 0.22 * (d.x * d.x - d.y * d.y)));\n"
    "  c = mix(c, vec3(0.04), smoothstep(w + 0.006, w, s));\n"
    "  return vec4(lit(c, view, 12.0, 0.15) * u_bright, 1.0);\n"
    "}\n";

/* ---- 2: tennis ball ---- */
static const char TENNIS[] =
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  vec3 d = ball(dir);\n"
    "  float fuzz = h3(floor(d * 160.0)) * 0.6 + h3(floor(d * 60.0)) * 0.4;\n"
    "  vec3 c = vec3(0.80, 0.90, 0.15) * (0.82 + 0.18 * fuzz);\n"
    "  float s = abs(seam8(d));\n"
    "  c = mix(c, vec3(0.97, 0.97, 0.92), smoothstep(0.045, 0.030, s));\n"
    "  c *= 1.0 - 0.25 * smoothstep(0.06, 0.045, s) * smoothstep(0.03, 0.045, s);\n"  /* groove */
    "  return vec4(lit(c, view, 6.0, 0.05) * u_bright, 1.0);\n"
    "}\n";

/* ---- 3: cricket ball ---- */
static const char CRICKET[] =
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  vec3 d = ball(dir);\n"
    "  vec3 c = vec3(0.55, 0.05, 0.06) * (0.9 + 0.1 * h3(floor(d * 90.0)));\n"
    "  float lon = atan(d.x, d.z);\n"
    "  float y = abs(d.y);\n"
    "  float band = smoothstep(0.045, 0.035, y);\n"                 /* raised seam */
    "  c = mix(c, vec3(0.62, 0.07, 0.08), band);\n"
    "  float rows = step(0.008, y) * step(y, 0.034);\n"
    "  float st = step(0.5, fract(lon * 90.0 / 6.2832 * 2.0 + (d.y > 0.0 ? 0.5 : 0.0)));\n"
    "  c = mix(c, vec3(0.95, 0.92, 0.85), rows * st);\n"            /* six rows of stitches */
    "  float q = min(abs(d.x), abs(d.z)) * step(0.06, y);\n"        /* quarter seams */
    "  c *= 1.0 - 0.35 * smoothstep(0.008, 0.003, q);\n"
    "  return vec4(lit(c, view, 60.0, 0.6) * u_bright, 1.0);\n"
    "}\n";

/* ---- 4: baseball ---- */
static const char BASEBALL[] =
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  vec3 d = ball(dir);\n"
    "  vec3 c = vec3(0.95, 0.93, 0.86) * (0.95 + 0.05 * h3(floor(d * 120.0)));\n"
    "  float s = seam8(d);\n"
    "  c *= 1.0 - 0.3 * smoothstep(0.012, 0.0, abs(s));\n"          /* the seam itself */
    "  float ph = atan(d.y, d.x) * 22.0 / 3.14159;\n"
    "  float k = fract(ph);\n"
    "  float side = abs(s) - 0.035;\n"                              /* chevron stitches */
    "  float v = abs(side - (abs(k - 0.5) - 0.25) * 0.05);\n"
    "  float st = smoothstep(0.009, 0.004, v) * step(abs(s), 0.07) * step(0.012, abs(s));\n"
    "  c = mix(c, vec3(0.80, 0.08, 0.08), st);\n"
    "  return vec4(lit(c, view, 18.0, 0.2) * u_bright, 1.0);\n"
    "}\n";

/* ---- 5: volleyball ---- */
static const char VOLLEY[] =
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  vec3 d = ball(dir);\n"
    "  vec3 a = abs(d);\n"
    "  float m = max(a.x, max(a.y, a.z));\n"
    "  float u, axis;\n"                                            /* strips run differently on each face */
    "  if (m == a.x) { u = d.y / a.x; axis = 0.0; }\n"
    "  else if (m == a.y) { u = d.z / a.y; axis = 1.0; }\n"
    "  else { u = d.x / a.z; axis = 2.0; }\n"
    "  float strip = floor((u + 1.0) * 1.5);\n"
    "  vec3 c = strip < 0.5 ? vec3(0.10, 0.30, 0.80) : strip < 1.5 ? vec3(0.97) : vec3(1.0, 0.80, 0.10);\n"
    "  if (mod(axis, 2.0) > 0.5) c = strip < 0.5 ? vec3(1.0, 0.80, 0.10) : strip < 1.5 ? vec3(0.10, 0.30, 0.80) : vec3(0.97);\n"
    "  float e1 = abs(fract((u + 1.0) * 1.5 + 0.5) - 0.5) / 1.5 * m;\n"
    "  float mid = a.x + a.y + a.z - m - min(a.x, min(a.y, a.z));\n"   /* the second largest */
    "  float e2 = m - mid;\n"                                       /* cube edges */
    "  c *= 0.45 + 0.55 * smoothstep(0.004, 0.012, min(e1, e2));\n"
    "  return vec4(lit(c, view, 25.0, 0.25) * u_bright, 1.0);\n"
    "}\n";

/* ---- 6: golf ball ---- */
static const char GOLF[] =
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  vec3 d = ball(dir);\n"
    "  float lat = asin(clamp(d.y, -1.0, 1.0)), lon = atan(d.x, d.z);\n"
    "  float rows = 22.0;\n"
    "  float rf = (lat / PI + 0.5) * rows, row = floor(rf);\n"
    "  float latc = ((row + 0.5) / rows - 0.5) * PI;\n"
    "  float n = max(1.0, floor(rows * 2.0 * cos(latc) + 0.5));\n"
    "  float cf = (lon / (2.0 * PI) + 0.5) * n + mod(row, 2.0) * 0.5;\n"
    "  vec2 q = vec2((fract(cf) - 0.5) * 2.0 * rows * cos(lat) / n, fract(rf) - 0.5);\n"
    "  float r = length(q) / 0.42;\n"
    /* each dimple is a shallow bowl: shade by which way its wall faces the light */
    "  vec2 wall = r < 1.0 ? q / max(length(q), 1e-3) * r : vec2(0.0);\n"
    "  float shade = 1.0 - 0.18 * smoothstep(0.6, 1.0, r) * step(r, 1.0) + dot(wall, vec2(0.12, -0.15));\n"
    "  vec3 c = vec3(0.96) * shade;\n"
    "  return vec4(lit(c, view, 40.0, 0.35) * u_bright, 1.0);\n"
    "}\n";

/* ---- 7: pool 8-ball ---- */
static const char POOL[] =
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  vec3 d = ball(dir);\n"
    "  vec3 c = vec3(0.025);\n"
    "  vec3 ax = vec3(0.0, 0.0, 1.0);\n"
    "  if (d.z < 0.0) { ax = -ax; }\n"                              /* a circle on each side */
    "  vec3 e = normalize(vec3(1.0, 0.0, 0.0));\n"
    "  float a = acos(clamp(dot(d, ax), -1.0, 1.0));\n"
    "  if (a < 0.42) {\n"
    "    c = vec3(0.96);\n"
    "    vec2 p = vec2(d.x * sign(ax.z), d.y) / 0.40;\n"            /* flat on the circle */
    "    float r1 = length(p - vec2(0.0, 0.30)), r2 = length(p - vec2(0.0, -0.25));\n"
    "    float ink = min(abs(r1 - 0.24), abs(r2 - 0.30));\n"        /* the 8 */
    "    c = mix(c, vec3(0.02), smoothstep(0.075, 0.05, ink));\n"
    "  }\n"
    "  c *= 1.0 - 0.3 * smoothstep(0.40, 0.42, a) * smoothstep(0.46, 0.42, a);\n"
    "  vec3 col = lit(c, view, 90.0, 1.2);\n"
    "  col += vec3(0.6) * pow(max(dot(reflect(-normalize(vec3(0.5, 0.3, 0.8)), view), vec3(0.0, 0.0, 1.0)), 0.0), 200.0) * u_shine;\n"
    "  return vec4(col * u_bright, 1.0);\n"
    "}\n";

/* ---- 8: beach ball ---- */
static const char BEACH[] =
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  vec3 d = ball(dir);\n"
    "  float lon = atan(d.x, d.z);\n"
    "  float g = floor((lon / 6.2832 + 0.5) * 6.0);\n"
    "  vec3 c = g < 1.0 ? vec3(0.95, 0.15, 0.15) : g < 2.0 ? vec3(0.98) : g < 3.0 ? vec3(1.0, 0.82, 0.10)\n"
    "         : g < 4.0 ? vec3(0.10, 0.45, 0.90) : g < 5.0 ? vec3(0.98) : vec3(0.15, 0.75, 0.30);\n"
    "  if (abs(d.y) > 0.92) c = vec3(0.98);\n"                      /* caps */
    "  float gf = fract((lon / 6.2832 + 0.5) * 6.0);\n"
    "  c *= 0.8 + 0.2 * smoothstep(0.0, 0.02, min(gf, 1.0 - gf) * sqrt(1.0 - d.y * d.y));\n"
    "  return vec4(lit(c, view, 50.0, 0.7) * u_bright, 1.0);\n"
    "}\n";

/* ---- 9: bowling ball ---- */
static const char BOWLING[] =
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  vec3 d = ball(dir);\n"
    "  vec3 p = d * 2.2;\n"
    "  p += 0.5 * vec3(sin(p.y * 2.1 + 1.0), sin(p.z * 1.7 + 2.0), sin(p.x * 2.3));\n"
    "  float sw = 0.5 + 0.5 * sin(p.x * 3.0 + p.y * 2.0 + sin(p.z * 4.0));\n"
    "  vec3 c = mix(vec3(0.10, 0.02, 0.25), vec3(0.15, 0.25, 0.75), smoothstep(0.3, 0.8, sw));\n"
    "  c = mix(c, vec3(0.75, 0.55, 0.95), smoothstep(0.85, 0.98, sw) * 0.5);\n"
    "  vec3 h1 = normalize(vec3(-0.17, 0.12, 1.0)), h2 = normalize(vec3(0.17, 0.12, 1.0)), h3v = normalize(vec3(0.0, -0.22, 1.0));\n"
    "  float hole = min(min(acos(clamp(dot(d, h1), -1.0, 1.0)), acos(clamp(dot(d, h2), -1.0, 1.0))),\n"
    "                   acos(clamp(dot(d, h3v), -1.0, 1.0)) * 0.8);\n"
    "  c = mix(c, vec3(0.0), smoothstep(0.07, 0.06, hole));\n"
    "  c *= 1.0 - 0.4 * smoothstep(0.085, 0.07, hole) * smoothstep(0.06, 0.07, hole);\n"
    "  return vec4(lit(c, view, 100.0, 1.0) * u_bright, 1.0);\n"
    "}\n";

static const char *GLSL[NSTYLE] = {
    FOOTBALL, BASKETBALL, TENNIS, CRICKET, BASEBALL, VOLLEY, GOLF, POOL, BEACH, BOWLING
};
static GLuint g_prog[NSTYLE];
static float  g_roll;

static void init(void)
{
    for (int i = 0; i < NSTYLE; i++) {
        size_t n = strlen(LIB) + strlen(GLSL[i]) + 1;
        char *src = malloc(n);
        snprintf(src, n, "%s%s", LIB, GLSL[i]);
        g_prog[i] = dome_surface_program_lib(DOME_GLSL_NOISE, src);
        free(src);
    }
}

static int style(void)
{
    int s = (int)lroundf(L.style);
    return s < 0 ? 0 : s >= NSTYLE ? NSTYLE - 1 : s;
}

static void update(const DomeFrame *f) { g_roll = fmodf(g_roll + f->dt * L.roll, 6.2832f); }

static void draw(const DomeFrame *f)
{
    GLuint p = g_prog[style()];
    dome_surface_begin(p, f);
    glUniform1f(glGetUniformLocation(p, "u_t"), f->now);
    glUniform1f(glGetUniformLocation(p, "u_roll"), g_roll);
    glUniform1f(glGetUniformLocation(p, "u_bright"), L.bright);
    glUniform1f(glGetUniformLocation(p, "u_shine"), L.shine);
    dome_surface_end();
}

static int key(SDL_Keycode k, int shift)
{
    (void)shift;
    if (k != SDLK_n) return 0;
    L.style = (float)((style() + 1) % NSTYLE);
    printf("Sports balls: %s\n", NAMES[style()]);
    return 1;
}

Scene scene_balls = {
    "SPORTS BALLS", "balls.cfg", 0, P, (int)(sizeof P / sizeof P[0]),
    init, NULL, update, draw, key,
    "N next ball", NULL
};
