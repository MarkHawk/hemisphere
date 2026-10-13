/*
 * retro.c  -  8-bit retro, in chunky pixels. N steps through the styles:
 *
 *   0 digital rain    green character columns pouring down
 *   1 copper bars     rolling colour bars with a sine-wave text scroller
 *   2 starfield       pixel stars flying out towards you
 *   3 pixel planet    an 8-bit world turning, with pixel clouds
 *   4 rim pong        two bats circling the rim, the ball crossing the dome
 *   5 rim defender    a ship circling the rim, aliens spiralling out and firing back
 *   6 radial blocks   a falling-block game where pieces fall inwards in rings
 *   7 16-colour plasma  the demo-scene favourite in a fixed palette
 *   8 tape loading    striped border while a picture loads line by line
 *   9 pixel fire      the classic demo fire effect
 *
 * All of them face you except the pixel planet, which turns with the
 * ball. The three games use the whole dome: the rim is the edge of play,
 * and each is played by a small robot. scale sets the size of the pixels. The aliens and every picture
 * here are made up for this program; none is copied from a real game.
 */
#include "../dome/dome.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NSTYLE 10
static const char *NAMES[NSTYLE] = {
    "digital rain", "copper bars", "starfield", "pixel planet", "rim pong",
    "rim defender", "radial blocks", "16-colour plasma", "tape loading", "pixel fire"
};

static struct { float style, speed, scale, bright, grid; } L = { 0.0f, 1.0f, 1.0f, 1.0f, 1.0f };

static Param P[] = {
    { "style",  &L.style,  1.0f, 0.0f, 9.0f, 0,
      "0 digital rain, 1 copper bars, 2 starfield, 3 pixel planet, 4 rim pong, 5 rim defender, "
      "6 radial blocks, 7 plasma, 8 tape loading, 9 pixel fire (N)" },
    { "speed",  &L.speed,  0.1f, 0.0f, 4.0f, 0, "speed of the animation" },
    { "scale",  &L.scale,  0.05f, 0.4f, 2.5f, 0, "pixel size (bigger = smaller pixels)" },
    { "bright", &L.bright, 0.05f, 0.2f, 2.0f, 0, "brightness" },
    { "grid",   &L.grid,   1.0f, 0.0f, 1.0f, 0, "0 smooth, 1 show the gaps between pixels" },
};

static const char LIB[] =
    "uniform float u_t, u_scale, u_bright, u_grid;\n"
    "uniform sampler2D u_board;\n"
    "uniform vec2 u_ball, u_vel, u_bats;\n"
    "vec2 plane(vec3 v){ return v.xy / (1.0 + max(v.z, 0.0)); }\n"
    "float NP(){ return floor(56.0 * u_scale + 0.5); }\n"          /* pixels across the front */
    "vec2 pix(vec2 p){ float n = NP(); return (floor(p * n) + 0.5) / n; }\n"
    "float gaps(vec2 p){ vec2 f = fract(p * NP()); return mix(1.0, 0.72 + 0.28 * step(0.13, min(f.x, f.y)), u_grid); }\n"
    "float tri(float x){ return abs(fract(x) * 2.0 - 1.0); }\n"
    "vec3 pal16(float i){\n"                                        /* a 16-colour home-computer palette */
    "  i = mod(floor(i), 16.0);\n"
    "  if (i < 1.0) return vec3(0.0);\n"
    "  if (i < 2.0) return vec3(0.21, 0.16, 0.48);\n"
    "  if (i < 3.0) return vec3(0.42, 0.23, 0.62);\n"
    "  if (i < 4.0) return vec3(0.60, 0.20, 0.30);\n"
    "  if (i < 5.0) return vec3(0.84, 0.33, 0.26);\n"
    "  if (i < 6.0) return vec3(0.89, 0.55, 0.20);\n"
    "  if (i < 7.0) return vec3(0.95, 0.80, 0.30);\n"
    "  if (i < 8.0) return vec3(0.98, 0.95, 0.60);\n"
    "  if (i < 9.0) return vec3(1.0);\n"
    "  if (i < 10.0) return vec3(0.65, 0.95, 0.70);\n"
    "  if (i < 11.0) return vec3(0.35, 0.80, 0.40);\n"
    "  if (i < 12.0) return vec3(0.15, 0.55, 0.45);\n"
    "  if (i < 13.0) return vec3(0.25, 0.70, 0.85);\n"
    "  if (i < 14.0) return vec3(0.45, 0.50, 0.95);\n"
    "  if (i < 15.0) return vec3(0.30, 0.30, 0.65);\n"
    "  return vec3(0.12, 0.10, 0.25);\n"
    "}\n";

/* ---- 0: digital rain ---- */
static const char RAIN[] =
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  vec2 p = plane(view);\n"
    "  float n = floor(30.0 * u_scale + 0.5);\n"
    "  vec2 g = p * n;\n"
    "  float col = floor(g.x), row = floor(g.y);\n"
    "  float h = h3(vec3(col, 1.0, 1.0));\n"
    "  float span = n * 2.6;\n"
    "  float head = n * 1.2 - mod(u_t * (5.0 + 9.0 * h) + h * 60.0, span);\n"
    "  float dist = row - floor(head);\n"
    "  float b = dist < 0.0 ? 0.0 : exp(-dist * 0.15);\n"
    "  vec2 f = fract(g);\n"
    "  vec2 sp = floor(vec2(f.x * 5.0, f.y * 7.0));\n"              /* a made-up glyph, 3 by 5 */
    "  float on = 0.0;\n"
    "  if (sp.x >= 1.0 && sp.x <= 3.0 && sp.y >= 1.0 && sp.y <= 5.0)\n"
    "    on = step(0.45, h3(vec3(col * 7.0 + sp.x, row * 3.0 + sp.y, floor(u_t * 1.5 + h3(vec3(col, row, 2.0)) * 9.0))));\n"
    "  vec3 c = vec3(0.1, 1.0, 0.35) * b * on;\n"
    "  if (dist >= 0.0 && dist < 1.0) c = vec3(0.85, 1.0, 0.9) * on;\n"   /* the leading glyph */
    "  return vec4(c * u_bright, 1.0);\n"
    "}\n";

/* ---- 1: copper bars (the scroller text is drawn on top as dots) ---- */
static const char COPPER[] =
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  vec2 p = pix(plane(view));\n"
    "  vec3 c = vec3(0.0, 0.0, 0.08);\n"
    "  for (int i = 0; i < 6; i++) {\n"
    "    float fi = float(i);\n"
    "    float y = 0.55 * sin(u_t * 0.9 + fi * 0.75);\n"
    "    float b = max(0.0, 1.0 - abs(p.y - y) / 0.075);\n"
    "    b = floor(b * 7.0) / 7.0;\n"                               /* banded, as on the real thing */
    "    vec3 k = fi < 1.0 ? vec3(1.0, 0.25, 0.1) : fi < 2.0 ? vec3(1.0, 0.65, 0.1) : fi < 3.0 ? vec3(0.95, 0.95, 0.2)\n"
    "           : fi < 4.0 ? vec3(0.2, 0.9, 0.3) : fi < 5.0 ? vec3(0.2, 0.6, 1.0) : vec3(0.75, 0.3, 1.0);\n"
    "    if (b > 0.0) c = k * b + vec3(1.0) * b * b * b * 0.4;\n"
    "  }\n"
    "  return vec4(c * gaps(plane(view)) * u_bright, 1.0);\n"
    "}\n";

/* ---- 2: starfield ---- */
static const char STARS[] =
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  vec2 p = plane(view), pp = pix(p);\n"
    "  float n = NP();\n"
    "  vec3 c = vec3(0.0);\n"
    "  for (int k = 0; k < 4; k++) {\n"
    "    float z = fract(u_t * 0.12 + float(k) * 0.25);\n"
    "    float s = mix(16.0, 0.9, z);\n"                            /* layers zoom out towards you */
    "    vec2 q = pp * s;\n"
    "    vec2 cell = floor(q);\n"
    "    float h = h3(vec3(cell, float(k) + floor(u_t * 0.12 + float(k) * 0.25) * 4.0));\n"
    "    if (h > 0.55) {\n"
    "      vec2 st = (cell + 0.5 + (vec2(h, fract(h * 7.3)) - 0.5) * 0.7) / s;\n"
    "      vec2 sp = (floor(st * n) + 0.5) / n;\n"
    "      float big = z > 0.8 ? 1.5 : 0.5;\n"
    "      if (max(abs(sp.x - pp.x), abs(sp.y - pp.y)) * n < big)\n"
    "        c = max(c, mix(vec3(0.3, 0.4, 0.9), vec3(1.0), z) * z * 1.4);\n"
    "    }\n"
    "  }\n"
    "  return vec4(c * gaps(p) * u_bright, 1.0);\n"
    "}\n";

/* ---- 3: pixel planet (turns with the ball) ---- */
static const char PLANET[] =
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  float rows = floor(36.0 * u_scale + 0.5);\n"
    "  float lat = asin(clamp(dir.y, -1.0, 1.0)), lon = atan(dir.x, dir.z);\n"
    "  vec2 g = vec2((lon / (2.0 * PI) + 0.5) * rows * 2.0, (lat / PI + 0.5) * rows);\n"
    "  vec2 cq = (floor(g) + 0.5) / vec2(rows * 2.0, rows);\n"
    "  float la = (cq.y - 0.5) * PI, lo = (cq.x - 0.5) * 2.0 * PI;\n"
    "  vec3 d = vec3(cos(la) * sin(lo), sin(la), cos(la) * cos(lo));\n"
    "  vec3 q = d * 2.3;\n"
    "  float h = sin(q.x * 1.9 + sin(q.y * 2.3)) + sin(q.z * 2.2 + sin(q.x * 1.7 + 1.0)) + 0.5 * sin(q.y * 4.1 + q.z * 3.3);\n"
    "  vec3 c = h < -0.2 ? vec3(0.10, 0.20, 0.60) : h < 0.25 ? vec3(0.15, 0.40, 0.85)\n"
    "         : h < 0.40 ? vec3(0.90, 0.80, 0.45) : h < 1.0 ? vec3(0.25, 0.70, 0.25)\n"
    "         : h < 1.5 ? vec3(0.10, 0.45, 0.15) : vec3(0.55, 0.45, 0.35);\n"
    "  if (abs(la) > 1.25) c = vec3(0.95);\n"                       /* ice caps */
    "  float cl = sin(lo * 5.0 + u_t * 0.3 + sin(la * 7.0)) + sin(lo * 3.0 - la * 9.0 - u_t * 0.2);\n"
    "  if (cl > 1.3) c = vec3(1.0);\n"
    "  float lt = dot(view, normalize(vec3(-0.5, 0.4, 0.75)));\n"
    "  c *= lt > 0.35 ? 1.0 : lt > 0.0 ? 0.65 : 0.3;\n"           /* three steps of shading */
    "  vec2 f = fract(g);\n"
    "  c *= mix(1.0, 0.75 + 0.25 * step(0.12, min(f.x, f.y)), u_grid);\n"
    "  return vec4(c * u_bright, 1.0);\n"
    "}\n";

/* ---- 4: rim pong (the game runs on the C side) ---- */
static const char PONG[] =
    "float adiff(float a, float b){ return abs(mod(a - b + 3.14159, 6.28318) - 3.14159); }\n"
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  vec2 p = view.xy, pp = pix(p);\n"
    "  float n = NP(), px = 1.0 / n;\n"
    "  float r = length(pp), a = atan(pp.y, pp.x);\n"
    "  vec3 c = vec3(0.0, 0.025, 0.0);\n"
    "  if (abs(r - 0.95) < px * 0.7 && fract(a * 40.0 / 6.2832) < 0.5) c = vec3(0.25, 0.40, 0.25);\n"   /* the rim */
    "  if (abs(pp.x) < px * 0.6 && fract(pp.y * n / 4.0) < 0.5 && r < 0.88) c = vec3(0.30, 0.45, 0.30);\n" /* the net */
    "  if (abs(r - 0.90) < px * 1.3 && (adiff(a, u_bats.x) < 0.22 || adiff(a, u_bats.y) < 0.22)) c = vec3(0.85, 1.0, 0.85);\n"
    "  vec2 tb = u_ball - u_vel * 0.15, ba = pp - tb, bv = u_ball - tb;\n"
    "  float h = clamp(dot(ba, bv) / max(dot(bv, bv), 1e-6), 0.0, 1.0);\n"
    "  if (length(ba - bv * h) < px * 0.9) c = max(c, vec3(0.30, 0.50, 0.30) * h);\n"  /* a short trail */
    "  if (max(abs(pp.x - u_ball.x), abs(pp.y - u_ball.y)) < px * 1.01) c = vec3(1.0);\n"
    "  return vec4(c * gaps(p) * u_bright, 1.0);\n"
    "}\n";

/* ---- 5: rim defender background (the ship, aliens and shots are dots) ---- */
static const char ALIENS[] =
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  vec2 p = view.xy, pp = pix(p);\n"
    "  float n = NP(), r = length(pp);\n"
    "  vec3 c = vec3(0.0, 0.0, 0.03);\n"
    "  vec2 g = floor(p * n);\n"
    "  if (h3(vec3(g, 1.0)) > 0.985) c = vec3(0.5, 0.55, 0.8) * (0.5 + 0.5 * sin(u_t * 2.0 + h3(vec3(g, 2.0)) * 30.0));\n"
    "  if (abs(fract(r * 6.0) - 0.5) > 0.5 - 3.0 / n && r < 0.88) c += vec3(0.03, 0.07, 0.09);\n"  /* faint range rings */
    "  c += vec3(0.6, 0.1, 0.5) * exp(-r * 16.0) * (0.7 + 0.3 * sin(u_t * 3.0));\n"                  /* the hive */
    "  if (abs(length(p) - 0.93) < 0.004) c = vec3(0.2, 0.6, 0.3);\n"                                /* the ship's track */
    "  return vec4(c * gaps(p) * u_bright, 1.0);\n"
    "}\n";

/* ---- 6: radial blocks: sectors round, rings outwards (board from C) ---- */
static const char BLOCKS[] =
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  vec2 p = view.xy;\n"
    "  float r = length(p), a = atan(p.y, p.x);\n"
    "  float r0 = 0.16, r1 = 0.93;\n"
    "  float ring = (r - r0) / (r1 - r0) * 9.0;\n"
    "  float sec = mod((a / 6.2832 + 0.5) * 16.0 + u_t * 0.15, 16.0);\n"   /* the board turns slowly */
    "  vec3 c = vec3(0.02, 0.02, 0.08);\n"
    "  c += vec3(0.5, 0.3, 1.0) * exp(-r * 10.0) * (0.8 + 0.2 * sin(u_t * 3.0));\n"
    "  if (r < r0 - 0.012) c = vec3(0.6, 0.4, 1.0) * (0.6 + 0.4 * sin(u_t * 2.0 - r * 30.0));\n"
    "  if (ring >= 0.0 && ring < 9.0) {\n"
    "    vec2 cell = vec2(floor(sec), floor(ring)), f = vec2(fract(sec), fract(ring));\n"
    "    vec4 t = texture2D(u_board, (cell + 0.5) / 16.0);\n"
    "    c = vec3(0.0, 0.0, 0.05) + vec3(0.04) * step(0.94, max(f.x, f.y));\n"
    "    if (t.a > 0.5) {\n"
    "      float e = min(min(f.x, f.y), min(1.0 - f.x, 1.0 - f.y));\n"
    "      c = t.rgb * (0.75 + 0.25 * smoothstep(0.0, 0.15, e));\n"
    "      if (f.y > 0.85 || f.x < 0.10) c = mix(c, vec3(1.0), 0.35);\n"
    "      if (f.y < 0.12 || f.x > 0.90) c *= 0.6;\n"
    "    }\n"
    "  }\n"
    "  if (abs(r - r1) < 0.006) c = vec3(0.55);\n"
    "  return vec4(c * u_bright, 1.0);\n"
    "}\n";

/* ---- 7: 16-colour plasma ---- */
static const char PLASMA16[] =
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  vec2 p = plane(view), q = pix(p);\n"
    "  float v = sin(q.x * 10.0 + u_t) + sin(q.y * 8.0 - u_t * 1.3)\n"
    "          + sin((q.x + q.y) * 7.0 + u_t * 0.7) + sin(length(q) * 12.0 - u_t * 1.6);\n"
    "  vec3 c = pal16((v + 4.0) * 2.0 + u_t * 2.0);\n"
    "  return vec4(c * gaps(p) * u_bright, 1.0);\n"
    "}\n";

/* ---- 8: tape loading ---- */
static const char TAPE[] =
    "vec3 picture(vec2 q){\n"                                       /* a made-up pixel sunset */
    "  vec3 c = q.y > 0.1 ? pal16(13.0) : q.y > -0.05 ? pal16(5.0) : pal16(4.0);\n"
    "  if (length(q - vec2(0.12, 0.06)) < 0.12) c = pal16(7.0);\n"
    "  if (q.y < -0.12 + 0.06 * sin(q.x * 14.0) + 0.04 * sin(q.x * 31.0)) c = pal16(3.0);\n"
    "  if (q.y < -0.24) c = pal16(1.0) * (0.8 + 0.2 * step(0.5, fract(q.y * 40.0 + u_t)));\n"
    "  return c;\n"
    "}\n"
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  vec2 p = plane(view), pp = pix(p);\n"
    "  float n = NP();\n"
    "  float cyc = fract(u_t / 14.0);\n"
    "  vec3 c;\n"
    "  if (abs(pp.x) < 0.48 && abs(pp.y) < 0.36) {\n"
    "    float shown = 0.36 - (cyc - 0.15) / 0.75 * 0.72;\n"        /* rows arrive from the top */
    "    c = pp.y > shown ? picture(pp) : vec3(0.85);\n"
    "  } else {\n"
    "    float row = floor(pp.y * n);\n"
    "    if (cyc < 0.15) c = mod(floor(row / 4.0 + u_t * 30.0), 2.0) < 1.0 ? vec3(0.9, 0.15, 0.15) : vec3(0.15, 0.85, 0.85);\n"
    "    else if (cyc < 0.9) c = h3(vec3(row, floor(u_t * 50.0), 3.0)) < 0.5 ? vec3(0.95, 0.9, 0.2) : vec3(0.2, 0.25, 0.9);\n"
    "    else c = vec3(0.85);\n"
    "  }\n"
    "  return vec4(c * gaps(p) * u_bright, 1.0);\n"
    "}\n";

/* ---- 9: pixel fire ---- */
static const char FIRE[] =
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  vec2 p = plane(view), q = pix(p);\n"
    "  float y = q.y + 0.65;\n"
    "  float tb = sin(q.x * 17.0 + sin(q.y * 11.0 + u_t * 4.0) - u_t * 2.0)\n"
    "           + sin(q.x * 7.0 + q.y * 9.0 - u_t * 5.0) + 0.5 * sin(q.x * 31.0 - u_t * 7.0 + q.y * 20.0);\n"
    "  float heat = 1.25 - y * 1.25 + 0.16 * tb;\n"
    "  heat *= smoothstep(0.95, 0.55, abs(q.x));\n"
    "  float lv = floor(clamp(heat, 0.0, 1.0) * 7.0);\n"
    "  vec3 c = lv < 1.0 ? vec3(0.0) : lv < 2.0 ? vec3(0.35, 0.02, 0.02) : lv < 3.0 ? vec3(0.65, 0.08, 0.02)\n"
    "         : lv < 4.0 ? vec3(0.90, 0.25, 0.03) : lv < 5.0 ? vec3(1.0, 0.50, 0.05)\n"
    "         : lv < 6.0 ? vec3(1.0, 0.80, 0.20) : vec3(1.0, 0.97, 0.75);\n"
    "  return vec4(c * gaps(p) * u_bright, 1.0);\n"
    "}\n";

static const char *GLSL[NSTYLE] = {
    RAIN, COPPER, STARS, PLANET, PONG, ALIENS, BLOCKS, PLASMA16, TAPE, FIRE
};
static GLuint g_prog[NSTYLE];
static float  g_t;

/* ---------------------------------------------------------------- */
/* Radial blocks: a falling-block game on rings. x is the sector (it  */
/* wraps all the way round), y the ring, 0 innermost; pieces fall     */
/* inwards. A robot tries every sector and turn and picks the best.   */
/* ---------------------------------------------------------------- */
#define BW 16                      /* sectors */
#define BH 11                      /* rings, the top two outside the rim */
static unsigned char g_board[BH][BW];
static int g_pc, g_pr, g_px, g_py, g_tx, g_tr;
static float g_tick;
static GLuint g_board_tex;

static const signed char SHAPES[7][4][2] = {   /* I O T S Z J L */
    { {0,0}, {1,0}, {2,0}, {3,0} }, { {0,0}, {1,0}, {0,1}, {1,1} },
    { {0,0}, {1,0}, {2,0}, {1,1} }, { {0,0}, {1,0}, {1,1}, {2,1} },
    { {1,0}, {2,0}, {0,1}, {1,1} }, { {0,0}, {1,0}, {2,0}, {0,1} },
    { {0,0}, {1,0}, {2,0}, {2,1} },
};
static const unsigned char BCOL[8][3] = {
    {0,0,0}, {40,220,230}, {240,220,40}, {180,60,230}, {60,220,80},
    {235,60,60}, {50,90,235}, {245,150,40},
};

static int wrap(int x) { return ((x % BW) + BW) % BW; }

static void cells(int pc, int rot, int out[4][2])
{
    for (int i = 0; i < 4; i++) {
        int x = SHAPES[pc][i][0], y = SHAPES[pc][i][1];
        for (int r = 0; r < rot; r++) { int t = x; x = y; y = -t; }
        out[i][0] = x; out[i][1] = y;
    }
}

static int fits(int pc, int rot, int px, int py)
{
    int c[4][2];
    cells(pc, rot, c);
    for (int i = 0; i < 4; i++) {
        int x = wrap(px + c[i][0]), y = py + c[i][1];
        if (y < 0) return 0;
        if (y < BH && g_board[y][x]) return 0;
    }
    return 1;
}

static float judge(int pc, int rot, int px)
{
    int py = BH - 2;
    if (!fits(pc, rot, px, py)) return -1e9f;
    while (fits(pc, rot, px, py - 1)) py--;
    unsigned char b[BH][BW];
    memcpy(b, g_board, sizeof b);
    int c[4][2];
    cells(pc, rot, c);
    for (int i = 0; i < 4; i++) {
        int y = py + c[i][1];
        if (y >= BH - 2) return -1e9f;          /* would poke out past the rim */
        b[y][wrap(px + c[i][0])] = 1;
    }
    int lines = 0, holes = 0, agg = 0, bump = 0, prev = -1, first = 0;
    for (int y = 0; y < BH; y++) {
        int full = 1;
        for (int x = 0; x < BW; x++) if (!b[y][x]) full = 0;
        lines += full;
    }
    for (int x = 0; x < BW; x++) {
        int h = 0;
        for (int y = BH - 1; y >= 0; y--) if (b[y][x]) { h = y + 1; break; }
        for (int y = 0; y < h; y++) if (!b[y][x]) holes++;
        agg += h;
        if (prev >= 0) bump += abs(h - prev); else first = h;
        prev = h;
    }
    bump += abs(prev - first);                  /* the ring wraps round */
    return lines * 0.76f - agg * 0.51f - holes * 0.36f - bump * 0.18f;
}

static void spawn(void)
{
    g_pc = rand() % 7;
    g_pr = 0; g_px = rand() % BW; g_py = BH - 2;
    float best = -1e10f;
    g_tx = g_px; g_tr = 0;
    for (int r = 0; r < 4; r++)
        for (int x = 0; x < BW; x++) {
            float sc = judge(g_pc, r, x) + (float)rand() / RAND_MAX * 0.05f;
            if (sc > best) { best = sc; g_tx = x; g_tr = r; }
        }
    if (!fits(g_pc, g_pr, g_px, g_py) || best < -1e8f) {   /* full up: start again */
        memset(g_board, 0, sizeof g_board);
        spawn();
    }
}

static void blocks_step(void)
{
    if (g_pr != g_tr && fits(g_pc, (g_pr + 1) & 3, g_px, g_py)) g_pr = (g_pr + 1) & 3;
    else if (g_px != g_tx) {
        int d = wrap(g_tx - g_px), step = d <= BW / 2 ? 1 : -1;   /* the short way round */
        if (fits(g_pc, g_pr, g_px + step, g_py)) g_px = wrap(g_px + step);
    }
    if (fits(g_pc, g_pr, g_px, g_py - 1)) { g_py--; return; }
    int c[4][2];
    cells(g_pc, g_pr, c);
    for (int i = 0; i < 4; i++) {
        int y = g_py + c[i][1];
        if (y < BH) g_board[y][wrap(g_px + c[i][0])] = (unsigned char)(g_pc + 1);
    }
    for (int y = 0; y < BH; y++) {
        int full = 1;
        for (int x = 0; x < BW; x++) if (!g_board[y][x]) full = 0;
        if (full) {
            memmove(&g_board[y][0], &g_board[y + 1][0], (size_t)(BH - 1 - y) * BW);
            memset(&g_board[BH - 1][0], 0, BW);
            y--;
        }
    }
    spawn();
}

static void blocks_upload(void)
{
    static unsigned char px[16][16][4];
    memset(px, 0, sizeof px);
    for (int y = 0; y < 9; y++)
        for (int x = 0; x < BW; x++)
            if (g_board[y][x]) { memcpy(px[y][x], BCOL[g_board[y][x]], 3); px[y][x][3] = 255; }
    int c[4][2];
    cells(g_pc, g_pr, c);
    for (int i = 0; i < 4; i++) {
        int x = wrap(g_px + c[i][0]), y = g_py + c[i][1];
        if (y >= 0 && y < 9) { memcpy(px[y][x], BCOL[g_pc + 1], 3); px[y][x][3] = 255; }
    }
    glBindTexture(GL_TEXTURE_2D, g_board_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
}

static float rnd(void) { return (float)rand() / RAND_MAX; }
static float angdiff(float a, float b)
{
    float d = fmodf(a - b + 3.0f * DOME_PI, 2.0f * DOME_PI) - DOME_PI;
    return d;
}

/* ---------------------------------------------------------------- */
/* Rim pong: the ball crosses the dome; each bat guards half the rim  */
/* ---------------------------------------------------------------- */
static float g_ball[2], g_vel[2], g_bats[2] = { DOME_PI, 0.0f }, g_wait;

static void pong_reset(void)
{
    float a = (rnd() < 0.5f ? 0.0f : DOME_PI) + (rnd() - 0.5f) * 1.2f;
    g_ball[0] = g_ball[1] = 0.0f;
    g_vel[0] = cosf(a) * 0.45f; g_vel[1] = sinf(a) * 0.45f;
    g_wait = 0.8f;
}

static void pong_step(float dt)
{
    const float RB = 0.90f;
    if (g_vel[0] == 0.0f && g_vel[1] == 0.0f) pong_reset();
    if (g_wait > 0) g_wait -= dt;
    else { g_ball[0] += g_vel[0] * dt; g_ball[1] += g_vel[1] * dt; }
    for (int s = 0; s < 2; s++) {
        float home = s == 0 ? DOME_PI : 0.0f, target = home;
        int coming = s == 0 ? g_vel[0] < 0 : g_vel[0] > 0;
        if (coming) {                                   /* where will it reach the rim? */
            float b = g_ball[0] * g_vel[0] + g_ball[1] * g_vel[1];
            float vv = g_vel[0] * g_vel[0] + g_vel[1] * g_vel[1];
            float cc = g_ball[0] * g_ball[0] + g_ball[1] * g_ball[1] - RB * RB;
            float k = (-b + sqrtf(fmaxf(b * b - vv * cc, 0.0f))) / vv;
            target = atan2f(g_ball[1] + g_vel[1] * k, g_ball[0] + g_vel[0] * k);
        }
        float d = angdiff(target, g_bats[s]);
        float mv = fminf(fabsf(d), 2.0f * dt);
        g_bats[s] += d > 0 ? mv : -mv;
        float off = angdiff(g_bats[s], home);           /* stay on your own half */
        if (off > 1.5708f) g_bats[s] = home + 1.5708f;
        if (off < -1.5708f) g_bats[s] = home - 1.5708f;
    }
    float r = sqrtf(g_ball[0] * g_ball[0] + g_ball[1] * g_ball[1]);
    if (r >= RB - 0.012f && g_ball[0] * g_vel[0] + g_ball[1] * g_vel[1] > 0) {
        int s = g_ball[0] < 0 ? 0 : 1;
        float a = atan2f(g_ball[1], g_ball[0]), d = angdiff(a, g_bats[s]);
        if (fabsf(d) < 0.24f) {
            float nx = g_ball[0] / r, ny = g_ball[1] / r, dv = 2.0f * (g_vel[0] * nx + g_vel[1] * ny);
            g_vel[0] -= dv * nx; g_vel[1] -= dv * ny;
            float tw = d * 1.2f, c = cosf(tw), sn = sinf(tw);   /* off-centre hits angle it */
            float vx = g_vel[0] * c - g_vel[1] * sn, vy = g_vel[0] * sn + g_vel[1] * c;
            if (vx * nx + vy * ny < 0) { g_vel[0] = vx; g_vel[1] = vy; }
            float sp = sqrtf(g_vel[0] * g_vel[0] + g_vel[1] * g_vel[1]);
            float want = fminf(sp * 1.04f, 1.0f) / sp;
            g_vel[0] *= want; g_vel[1] *= want;
        } else if (r > 0.97f) pong_reset();             /* missed */
    }
}

/* ---------------------------------------------------------------- */
/* Rim defender: aliens spiral out of the hive at the centre and fire */
/* back; the ship circles the rim, shooting inwards. Lives, score and */
/* waves, played by a robot that aims ahead and dodges incoming fire. */
/* Everything is drawn as pixel dots on the front of the dome.        */
/* ---------------------------------------------------------------- */
#define NAL 32
#define NSHOT 16
#define NBOMB 12
#define NEXP 16
#define NPART 48
typedef struct { int on, kind; float r, a, w, cool; } Alien;
typedef struct { int on; float r, a; } Shot;
typedef struct { int on; float x, y, vx, vy; } Bomb;
typedef struct { int on, big; float x, y, t; float vx[NPART], vy[NPART]; float col[3]; } Boom;
static Alien g_al[NAL];
static Shot  g_sh[NSHOT];
static Bomb  g_bo[NBOMB];
static Boom  g_ex[NEXP];
static float g_ship = 0.0f, g_cool, g_spawn, g_dead, g_safe, g_banner, g_over;
static int   g_lives = 3, g_score, g_hi, g_wave = 1, g_left;   /* aliens still to come this wave */
static DomePoints g_pix;

/* made-up sprites, 8 wide, two frames each */
static const char *SPR[3][2][6] = {
    { { "..####..", ".######.", "##.##.##", "########", ".#.##.#.", "#......#" },
      { "..####..", ".######.", "##.##.##", "########", ".#.##.#.", ".#....#." } },
    { { "...##...", "..####..", ".##..##.", "########", "..#..#..", ".#.##.#." },
      { "...##...", "..####..", ".##..##.", "########", "..#..#..", "#..##..#" } },
    { { "..####..", ".#....#.", "#..##..#", "#.####.#", ".#....#.", "..#..#.." },
      { "..####..", ".#....#.", "#..##..#", "#.####.#", ".#....#.", "...##..." } },
};
static const char *SHIP[5] = { "...#...", "...#...", "..###..", ".#####.", "##.#.##" };
static const float ACOL[3][3] = { { 0.3f, 1.0f, 0.4f }, { 0.3f, 0.8f, 1.0f }, { 1.0f, 0.4f, 0.9f } };
static const float RSHIP = 0.89f;

static void plate_dot(float x, float y, float r, float g, float b, float a)
{
    float rr = x * x + y * y;
    if (rr > 0.985f) return;
    dome_points_push(&g_pix, x, y, sqrtf(1.0f - rr), r, g, b, a);
}

/* draw a sprite at radius rad, angle ang, its top pointing at the centre */
static void sprite(const char *const *rows, int h, float rad, float ang, float px,
                   float r, float g, float b, float a)
{
    float cx = rad * cosf(ang), cy = rad * sinf(ang);
    float ux = -cosf(ang), uy = -sinf(ang);          /* up: towards the centre */
    float rx = -uy, ry = ux;                          /* right */
    int w = (int)strlen(rows[0]);
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
            if (rows[j][i] != '#') continue;
            float sx = (i - 0.5f * (w - 1)) * px, sy = (0.5f * (h - 1) - j) * px;
            plate_dot(cx + sx * rx + sy * ux, cy + sx * ry + sy * uy, r, g, b, a);
        }
}

static void boom(float x, float y, int big, const float *col)
{
    for (int i = 0; i < NEXP; i++)
        if (!g_ex[i].on) {
            Boom *e = &g_ex[i];
            e->on = 1; e->big = big; e->x = x; e->y = y; e->t = 0.0f;
            memcpy(e->col, col, sizeof e->col);
            for (int k = 0; k < NPART; k++) {
                float a = rnd() * 6.2832f, sp = (big ? 0.35f : 0.18f) * (0.3f + 0.7f * rnd());
                e->vx[k] = cosf(a) * sp; e->vy[k] = sinf(a) * sp;
            }
            return;
        }
}

static void new_wave(void)
{
    g_left = 12 + 4 * g_wave;
    g_banner = 2.5f;
}

static void new_game(void)
{
    memset(g_al, 0, sizeof g_al); memset(g_sh, 0, sizeof g_sh); memset(g_bo, 0, sizeof g_bo);
    g_lives = 3; g_score = 0; g_wave = 1; g_dead = 0; g_safe = 2.0f;
    new_wave();
}

static void ship_hit(void)
{
    static const float SC[3] = { 1.0f, 0.85f, 0.3f };
    float x = RSHIP * cosf(g_ship), y = RSHIP * sinf(g_ship);
    boom(x, y, 1, SC);
    boom(x * 0.97f, y * 0.97f, 1, (const float[3]){ 1.0f, 0.35f, 0.1f });
    g_dead = 2.5f;
    memset(g_bo, 0, sizeof g_bo);
    if (--g_lives <= 0) g_over = 4.0f;
}

static void defender_step(float dt)
{
    if (g_lives <= 0 && g_over <= 0) new_game();
    static int started = 0;
    if (!started) { started = 1; new_game(); }
    if (g_over > 0) { g_over -= dt; }
    if (g_banner > 0) g_banner -= dt;
    if (g_dead > 0) { g_dead -= dt; if (g_dead <= 0 && g_lives > 0) g_safe = 2.0f; }
    else if (g_safe > 0) g_safe -= dt;
    float speed = 1.0f + 0.15f * (g_wave - 1);

    /* aliens leave the hive */
    g_spawn -= dt;
    if (g_spawn <= 0 && g_left > 0 && g_over <= 0) {
        g_spawn = fmaxf(0.8f, 1.6f - 0.1f * g_wave);          /* a group of three at a time */
        float a0 = rnd() * 6.2832f, w0 = (rnd() < 0.5f ? -1.0f : 1.0f) * (0.2f + 0.3f * rnd());
        int kind = rand() % 3;
        for (int n = 0, i = 0; n < 3 && i < NAL && g_left > 0; i++)
            if (!g_al[i].on) {
                g_al[i] = (Alien){ 1, kind, 0.04f, a0 + n * 0.35f, w0, 1.0f + 2.0f * rnd() };
                g_left--; n++;
            }
    }
    int alive = 0, target = -1;
    float best = -1.0f;
    for (int i = 0; i < NAL; i++) {
        Alien *a = &g_al[i];
        if (!a->on) continue;
        alive++;
        a->r += 0.055f * speed * dt * (1.0f + a->r);
        a->a += a->w * speed * dt;
        float x = a->r * cosf(a->a), y = a->r * sinf(a->a);
        /* they fire back once they are well out */
        a->cool -= dt;
        if (a->cool <= 0 && a->r > 0.3f && g_dead <= 0 && g_over <= 0) {
            a->cool = 1.5f + 3.0f * rnd() / speed;
            for (int k = 0; k < NBOMB; k++)
                if (!g_bo[k].on) {
                    float tx = RSHIP * cosf(g_ship) - x, ty = RSHIP * sinf(g_ship) - y;
                    float l = sqrtf(tx * tx + ty * ty) + 1e-4f, v = 0.32f * speed;
                    g_bo[k] = (Bomb){ 1, x, y, tx / l * v, ty / l * v };
                    break;
                }
        }
        if (a->r > RSHIP - 0.03f) {                    /* reached the rim */
            a->on = 0;
            boom(x, y, 0, ACOL[a->kind]);
            if (g_dead <= 0 && g_safe <= 0 && fabsf(angdiff(a->a, g_ship)) < 0.14f) ship_hit();
            continue;
        }
        /* the most dangerous: furthest out, weighted by how near the ship it is */
        float danger = a->r - 0.15f * fabsf(angdiff(a->a, g_ship));
        if (a->r > 0.25f && danger > best) { best = danger; target = i; }
    }
    if (g_left == 0 && alive == 0 && g_over <= 0) { g_wave++; new_wave(); }

    /* the robot: dodge incoming fire first, otherwise line up and shoot */
    if (g_dead <= 0 && g_over <= 0) {
        float want = g_ship;
        int dodging = 0;
        for (int k = 0; k < NBOMB; k++) {
            Bomb *b = &g_bo[k];
            if (!b->on) continue;
            float br = sqrtf(b->x * b->x + b->y * b->y), ba = atan2f(b->y, b->x);
            float d = angdiff(ba, g_ship);
            if (br > 0.6f && fabsf(d) < 0.18f) { want = g_ship - (d > 0 ? 0.6f : -0.6f); dodging = 1; break; }
        }
        if (!dodging && target >= 0) {
            Alien *a = &g_al[target];
            want = a->a + a->w * speed * (RSHIP - a->r) / 1.1f;    /* lead the shot */
        }
        float d = angdiff(want, g_ship), mv = fminf(fabsf(d), 2.4f * dt);
        g_ship += d > 0 ? mv : -mv;
        g_cool -= dt;
        if (g_cool <= 0) {                             /* rapid fire at anything in line */
            for (int k = 0; k < NAL; k++)
                if (g_al[k].on && g_al[k].r > 0.32f &&
                    fabsf(angdiff(g_al[k].a + g_al[k].w * speed * (RSHIP - g_al[k].r) / 1.1f, g_ship)) < 0.10f) {
                    for (int q = 0; q < NSHOT; q++)
                        if (!g_sh[q].on) { g_sh[q] = (Shot){ 1, RSHIP - 0.04f, g_ship }; break; }
                    g_cool = 0.12f;
                    break;
                }
        }
    }

    for (int i = 0; i < NSHOT; i++) {
        Shot *s = &g_sh[i];
        if (!s->on) continue;
        s->r -= 1.1f * dt;
        if (s->r < 0.03f) { s->on = 0; continue; }
        float sx = s->r * cosf(s->a), sy = s->r * sinf(s->a);
        for (int k = 0; k < NAL; k++) {
            Alien *a = &g_al[k];
            if (!a->on) continue;
            float hit = 0.03f + 0.03f * a->r;          /* they grow as they come out */
            float dx = sx - a->r * cosf(a->a), dy = sy - a->r * sinf(a->a);
            if (dx * dx + dy * dy < hit * hit) {
                a->on = 0; s->on = 0;
                boom(a->r * cosf(a->a), a->r * sinf(a->a), 0, ACOL[a->kind]);
                g_score += 10 * (a->kind + 1) * g_wave;
                if (g_score > g_hi) g_hi = g_score;
                break;
            }
        }
    }
    for (int i = 0; i < NBOMB; i++) {
        Bomb *b = &g_bo[i];
        if (!b->on) continue;
        b->x += b->vx * dt; b->y += b->vy * dt;
        if (b->x * b->x + b->y * b->y > 0.97f) { b->on = 0; continue; }
        float dx = b->x - RSHIP * cosf(g_ship), dy = b->y - RSHIP * sinf(g_ship);
        if (g_dead <= 0 && g_safe <= 0 && dx * dx + dy * dy < 0.035f * 0.035f) { b->on = 0; ship_hit(); }
    }
    for (int i = 0; i < NEXP; i++)
        if (g_ex[i].on && (g_ex[i].t += dt) > (g_ex[i].big ? 1.4f : 0.7f)) g_ex[i].on = 0;
}

static void defender_draw(float t)
{
    dome_points_clear(&g_pix);
    int frame = (int)(t * 3.0f) & 1;
    for (int i = 0; i < NAL; i++) {
        Alien *a = &g_al[i];
        if (!a->on) continue;
        const float *c = ACOL[a->kind];
        sprite(SPR[a->kind][frame], 6, a->r, a->a, 0.008f + 0.014f * a->r, c[0], c[1], c[2], 1.0f);
    }
    /* the ship, blinking while it is safe after a respawn */
    if (g_dead <= 0 && g_lives > 0 && (g_safe <= 0 || ((int)(t * 8.0f) & 1))) {
        sprite(SHIP, 5, RSHIP, g_ship, 0.015f, 1.0f, 0.95f, 0.5f, 1.0f);
        if (g_cool > 0.07f) plate_dot((RSHIP - 0.05f) * cosf(g_ship), (RSHIP - 0.05f) * sinf(g_ship), 1, 1, 1, 1);
    }
    for (int i = 0; i < NSHOT; i++)
        if (g_sh[i].on)
            for (int k = 0; k < 4; k++)
                plate_dot((g_sh[i].r + k * 0.01f) * cosf(g_sh[i].a), (g_sh[i].r + k * 0.01f) * sinf(g_sh[i].a), 1.0f, 1.0f, 0.6f, 1.0f - k * 0.2f);
    for (int i = 0; i < NBOMB; i++)
        if (g_bo[i].on) {                             /* zig-zag enemy fire */
            float zz = ((int)(t * 12.0f) & 1) ? 0.008f : -0.008f;
            float l = sqrtf(g_bo[i].vx * g_bo[i].vx + g_bo[i].vy * g_bo[i].vy) + 1e-4f;
            float nx = -g_bo[i].vy / l, ny = g_bo[i].vx / l;
            for (int k = 0; k < 3; k++)
                plate_dot(g_bo[i].x - g_bo[i].vx / l * k * 0.012f + nx * zz * (k == 1 ? -1 : 1),
                          g_bo[i].y - g_bo[i].vy / l * k * 0.012f + ny * zz * (k == 1 ? -1 : 1), 1.0f, 0.3f, 0.3f, 1.0f);
        }
    for (int i = 0; i < NEXP; i++) {
        Boom *e = &g_ex[i];
        if (!e->on) continue;
        float life = e->big ? 1.4f : 0.7f, f = 1.0f - e->t / life;
        for (int k = 0; k < NPART; k++) {
            float x = e->x + e->vx[k] * e->t, y = e->y + e->vy[k] * e->t;
            const float *c = (k % 3 == 0) ? (const float[3]){ 1.0f, 1.0f, 0.8f } : e->col;
            plate_dot(x, y, c[0], c[1], c[2], f);
        }
        if (e->t < 0.12f)                             /* the flash */
            for (int k = 0; k < 16; k++)
                plate_dot(e->x + cosf(k * 0.39f) * 0.02f, e->y + sinf(k * 0.39f) * 0.02f, 1, 1, 1, 1);
    }
    /* score, lives and wave round the rim, outside the ship's track. The
     * bottom arc reads left to right: score, lives, wave; HI on the top */
    char buf[48];
    const float HR = 78.0f, HH = 4.5f;               /* arc radius and letter height, degrees */
    snprintf(buf, sizeof buf, "SCORE %05d", g_score);
    dome_text_arc(&g_pix, buf, HR, -140.0f, HH, 0.9f, 0.9f, 1.0f, 0.95f);
    dome_text_arc(&g_pix, "LIVES =", HR, -93.0f, HH, 1.0f, 0.95f, 0.5f, 0.95f);
    for (int k = 0; k < g_lives && k < 5; k++)      /* one ship for each life left */
        sprite(SHIP, 5, sinf(DEG2RAD(HR)), DEG2RAD(-76.0f + k * 5.0f), 0.010f, 1.0f, 0.95f, 0.5f, 0.95f);
    snprintf(buf, sizeof buf, "WAVE %d", g_wave);
    dome_text_arc(&g_pix, buf, HR, -45.0f, HH, 0.6f, 1.0f, 0.6f, 0.95f);
    snprintf(buf, sizeof buf, "HI %05d", g_hi);
    dome_text_arc(&g_pix, buf, HR, 90.0f, HH, 0.8f, 0.8f, 0.9f, 0.8f);
    if (g_over > 0)
        dome_text_arc(&g_pix, "GAME OVER", 25.0f, 90.0f, 7.0f, 1.0f, 0.3f, 0.3f, 1.0f);
    else if (g_banner > 0)
        { snprintf(buf, sizeof buf, "WAVE %d", g_wave); dome_text_arc(&g_pix, buf, 25.0f, 90.0f, 7.0f, 0.3f, 1.0f, 0.4f, 1.0f); }
    dome_points_upload(&g_pix);
}

/* ---------------------------------------------------------------- */
/* Copper bars: the scroller, letters riding a sine wave             */
/* ---------------------------------------------------------------- */
static DomePoints g_scroll;
static const char SCROLL_TEXT[] =
    "HEMISPHERE ... GREETINGS FROM A RASPBERRY PI ... "
    "COPPER BARS AND A SINE SCROLLER, JUST LIKE 1989 ... "
    "BUILT ON A GLOBE ... PRESS N FOR MORE ...      ";

static void scroller(float t)
{
    dome_points_clear(&g_scroll);
    const float H = 6.0f;                          /* letter height, degrees */
    const float CW = DEG2RAD(H) / 7.0f * 6.0f;     /* one character, radians */
    int len = (int)strlen(SCROLL_TEXT);
    float pos = t * 0.35f / CW;                    /* characters scrolled */
    int first = (int)floorf(pos);
    for (int k = -14; k <= 14; k++) {
        int ci = ((first + k) % len + len) % len;
        char ch[2] = { SCROLL_TEXT[ci], 0 };
        if (ch[0] == ' ') continue;
        float lon = ((float)(first + k) - pos) * CW;
        float lat = 9.0f * sinf(t * 2.0f + lon * 5.0f);
        float r, g, b;
        r = 0.5f + 0.5f * cosf(6.2832f * (lon * 0.6f + t * 0.1f));
        g = 0.5f + 0.5f * cosf(6.2832f * (lon * 0.6f + t * 0.1f + 0.33f));
        b = 0.5f + 0.5f * cosf(6.2832f * (lon * 0.6f + t * 0.1f + 0.67f));
        int before = g_scroll.n;
        dome_text_line(&g_scroll, ch, lat, H, r * 0.5f + 0.5f, g * 0.5f + 0.5f, b * 0.5f + 0.5f, 1.0f);
        float cl = cosf(lon), sl = sinf(lon);      /* move the letter round to its place */
        for (int i = before; i < g_scroll.n; i++) {
            DomeVert *v = &g_scroll.v[i];
            float x = v->x * cl + v->z * sl, z = -v->x * sl + v->z * cl;
            v->x = x; v->z = z;
        }
    }
    dome_points_upload(&g_scroll);
}

/* ---------------------------------------------------------------- */
static void init(void)
{
    for (int i = 0; i < NSTYLE; i++) {
        size_t n = strlen(LIB) + strlen(GLSL[i]) + 1;
        char *src = malloc(n);
        snprintf(src, n, "%s%s", LIB, GLSL[i]);
        g_prog[i] = dome_surface_program_lib(DOME_GLSL_NOISE, src);
        free(src);
    }
    glGenTextures(1, &g_board_tex);
    glBindTexture(GL_TEXTURE_2D, g_board_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    spawn();
    blocks_upload();
    dome_points_init(&g_scroll, 30000);
    dome_points_init(&g_pix, 60000);
    pong_reset();
}

static int style(void)
{
    int s = (int)lroundf(L.style);
    return s < 0 ? 0 : s >= NSTYLE ? NSTYLE - 1 : s;
}

static void update(const DomeFrame *f)
{
    g_t += f->dt * L.speed;
    float dt = fminf(f->dt, 0.1f) * L.speed;
    int s = style();
    if (s == 4) pong_step(dt);
    if (s == 5) defender_step(dt);
    if (s == 6) {
        g_tick += dt;
        int moved = 0;
        while (g_tick > 0.12f) { g_tick -= 0.12f; blocks_step(); moved = 1; }
        if (moved) blocks_upload();
    }
}

static void draw(const DomeFrame *f)
{
    int s = style();
    GLuint p = g_prog[s];
    dome_surface_begin(p, f);
    glUniform1f(glGetUniformLocation(p, "u_t"), g_t);
    glUniform1f(glGetUniformLocation(p, "u_scale"), L.scale);
    glUniform1f(glGetUniformLocation(p, "u_bright"), L.bright);
    glUniform1f(glGetUniformLocation(p, "u_grid"), floorf(L.grid + 0.5f));
    if (s == 4) {
        glUniform2f(glGetUniformLocation(p, "u_ball"), g_ball[0], g_ball[1]);
        glUniform2f(glGetUniformLocation(p, "u_vel"), g_vel[0], g_vel[1]);
        glUniform2f(glGetUniformLocation(p, "u_bats"), g_bats[0], g_bats[1]);
    }
    if (s == 6) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, g_board_tex);
        glUniform1i(glGetUniformLocation(p, "u_board"), 1);
        glActiveTexture(GL_TEXTURE0);
    }
    dome_surface_end();
    if (s == 5) {
        defender_draw(g_t);
        mat3 still; m3_identity(still);
        dome_draw_points(&g_pix, still, f->fade, 5.0f);
    }
    if (s == 1) {
        scroller(g_t);
        mat3 r;
        dome_view_rot(r);
        dome_draw_points(&g_scroll, r, f->fade, 2.5f);
    }
}

static int key(SDL_Keycode k, int shift)
{
    (void)shift;
    if (k != SDLK_n) return 0;
    L.style = (float)((style() + 1) % NSTYLE);
    printf("Retro: %s\n", NAMES[style()]);
    return 1;
}

Scene scene_retro = {
    "8-BIT RETRO", "retro.cfg", 0, P, (int)(sizeof P / sizeof P[0]),
    init, NULL, update, draw, key,
    "N next style", NULL
};
