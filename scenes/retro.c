/*
 * retro.c  -  8-bit retro, in chunky pixels. N steps through the styles:
 *
 *   0 digital rain    green character columns pouring down
 *   1 copper bars     rolling colour bars with a sine-wave text scroller
 *   2 starfield       pixel stars flying out towards you
 *   3 pixel planet    an 8-bit world turning, with pixel clouds
 *   4 maze chase      a robot clears a maze of dots, chased by four spiky
 *                     chasers; it stays in the middle and the maze slides round it
 *   5 rim defender    a ship circling the rim, aliens spiralling out and firing back
 *   6 asteroids       asteroids on a globe: the whole sphere is the playfield
 *   7 16-colour plasma  the demo-scene favourite in a fixed palette
 *   8 tape loading    striped border while a picture loads line by line
 *   9 pixel fire      the classic demo fire effect
 *
 * All of them face you except the pixel planet, which turns with the
 * ball, and asteroids, which follows the ship round the globe. The three
 * games are full games (levels, lives, score, high score), each played by
 * a small robot. scale sets the size of the pixels. The aliens and every picture
 * here are made up for this program; none is copied from a real game.
 */
#include "../dome/dome.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NSTYLE 10
static const char *NAMES[NSTYLE] = {
    "digital rain", "copper bars", "starfield", "pixel planet", "maze chase",
    "rim defender", "asteroids", "16-colour plasma", "tape loading", "pixel fire"
};

static struct { float style, speed, scale, bright, grid; } L = { 0.0f, 1.0f, 1.0f, 1.0f, 1.0f };

static Param P[] = {
    { "style",  &L.style,  1.0f, 0.0f, 9.0f, 0,
      "0 digital rain, 1 copper bars, 2 starfield, 3 pixel planet, 4 maze chase, 5 rim defender, "
      "6 asteroids, 7 plasma, 8 tape loading, 9 pixel fire (N)" },
    { "speed",  &L.speed,  0.1f, 0.0f, 4.0f, 0, "speed of the animation" },
    { "scale",  &L.scale,  0.05f, 0.4f, 2.5f, 0, "pixel size (bigger = smaller pixels)" },
    { "bright", &L.bright, 0.05f, 0.2f, 2.0f, 0, "brightness" },
    { "grid",   &L.grid,   1.0f, 0.0f, 1.0f, 0, "0 smooth, 1 show the gaps between pixels" },
};

static const char LIB[] =
    "uniform float u_t, u_scale, u_bright, u_grid;\n"
    "uniform vec4 u_g;\n"                                          /* game state for the games' backdrops */
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

/* ---- 4: maze chase. The robot is always in the middle; u_hero = robot
 * (maze x, y, facing, mouth/anim), u_c0..3 chasers (x, y, facing, look:
 * 0 normal, 1 blue, 2 blue flashing white, 3 eyes only, 4 hidden),
 * u_fruit (x, y, kind, on), u_mz (tiles from the middle to the rim,
 * level colour, unused, death 0..1), u_gl = the planet level (on, view
 * centre lat, lon, tile size in radians): the maze laid on a globe ---- */
static const char MAZE[] =
    "uniform sampler2D u_mwall, u_mdots;\n"
    "uniform vec4 u_hero, u_c0, u_c1, u_c2, u_c3, u_fruit, u_mz, u_gl;\n"
    "vec2 mwrap(vec2 q){ q.x = mod(q.x + 13.5, 27.0) - 13.5; return q; }\n"
    "vec3 robot(vec2 q, float dir, float sc, float death, inout float a){\n"
    "  q /= sc;\n"
    "  float r = length(q);\n"
    "  float rad = 0.44 * (1.0 - death);\n"
    "  if (r > rad + 0.25) return vec3(0.0);\n"
    "  float an = dir * 1.5708;\n"
    "  vec2 f = vec2(cos(an) * q.x + sin(an) * q.y, -sin(an) * q.x + cos(an) * q.y);\n"   /* x = facing */
    "  vec3 c = vec3(0.0);\n"
    "  if (r < rad) {\n"
    "    c = mix(vec3(0.75, 0.95, 1.0), vec3(0.15, 0.55, 0.85), smoothstep(0.0, rad, length(q - vec2(-0.12, -0.12))));\n"
    "    if (abs(f.y) < 0.09 && f.x > 0.05) c = vec3(0.05, 0.1, 0.2);\n"                      /* the visor */
    "    if (length(f - vec2(0.24, 0.0)) < 0.06) c = vec3(1.0, 0.35, 0.2) * (0.7 + 0.3 * sin(u_t * 12.0));\n"
    "    a = 1.0;\n"
    "  }\n"
    "  vec2 ant = vec2(-0.30, 0.32) * (1.0 - death);\n"                                        /* two little aerials */
    "  if (length(f - ant) < 0.05 || length(f - vec2(ant.x, -ant.y)) < 0.05) { c = vec3(1.0, 0.9, 0.3); a = 1.0; }\n"
    "  return c;\n"
    "}\n"
    "vec3 chaser(vec2 q, vec4 s, vec3 col, inout float a){\n"
    "  float r = length(q);\n"
    "  if (r > 0.6 || s.w > 3.5) return vec3(0.0);\n"
    "  float an = atan(q.y, q.x);\n"
    "  float rr = 0.34 + 0.07 * sin(an * 7.0 + u_t * 9.0);\n"                                   /* spiky and wriggling */
    "  vec3 c = vec3(0.0);\n"
    "  if (s.w < 2.5 && r < rr) {\n"
    "    vec3 body = s.w < 0.5 ? col : vec3(0.15, 0.2, 0.95);\n"
    "    if (s.w > 1.5) body = mix(body, vec3(0.95), 0.5 + 0.5 * sin(u_t * 12.0));\n"
    "    c = body * (0.65 + 0.35 * smoothstep(rr, 0.0, r)); a = 1.0;\n"
    "  }\n"
    "  vec2 look = s.w < 0.5 || s.w > 2.5 ? vec2(cos(s.z * 1.5708), sin(s.z * 1.5708)) * 0.05 : vec2(0.0);\n"
    "  for (int k = 0; k < 2; k++) {\n"
    "    vec2 e = vec2(k == 0 ? -0.12 : 0.12, -0.06);\n"
    "    if (length(q - e) < 0.09) { c = vec3(1.0); a = 1.0; }\n"
    "    if (length(q - e - look) < 0.045) { c = s.w > 0.5 && s.w < 2.5 ? vec3(1.0, 0.6, 0.6) : vec3(0.05, 0.05, 0.3); a = 1.0; }\n"
    "  }\n"
    "  return c;\n"
    "}\n"
    "vec3 lvlcol(float l){ return 0.5 + 0.5 * cos(6.2832 * (l * 0.17 + vec3(0.55, 0.75, 0.95))); }\n"
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  vec2 p = view.xy;\n"
    "  float R = u_mz.x;\n"
    "  vec3 c = vec3(0.0, 0.0, 0.03);\n"
    "  float h = h3(vec3(floor(p * 70.0), 5.0));\n"
    "  if (h > 0.992) c += vec3(0.3);\n"
    "  float a = 0.0;\n"
    "  vec2 m = u_hero.xy + vec2(p.x, -p.y) * R;\n"
    "  float onmaze = 1.0;\n"
    "  if (u_gl.x > 0.5) {\n"                       /* the planet level: the maze on a globe */
    "    float la = u_gl.y, lo = u_gl.z;\n"
    "    vec3 up = vec3(cos(la) * sin(lo), sin(la), cos(la) * cos(lo));\n"
    "    vec3 ea = vec3(cos(lo), 0.0, -sin(lo));\n"
    "    vec3 no = cross(up, ea);\n"
    "    vec3 e = normalize(ea * view.x + no * view.y + up * view.z);\n"
    "    float lat = asin(clamp(e.y, -1.0, 1.0)), lon = atan(e.x, e.z);\n"
    "    m = vec2(13.0 + lon / u_gl.w, 11.5 - lat / u_gl.w);\n"
    "    onmaze = step(-0.5, m.x) * step(m.x, 26.5);\n"
    "    float g = h3(floor(e * 40.0));\n"           /* a dark rocky planet round the maze */
    "    c = vec3(0.10, 0.07, 0.12) * (0.6 + 0.4 * g) * (0.4 + 0.6 * view.z);\n"
    "    c += vec3(0.2, 0.3, 0.8) * pow(1.0 - clamp(view.z, 0.0, 1.0), 3.0) * 0.5;\n"
    "    if (onmaze < 0.5) m.y = -50.0;\n"
    "  }\n"
    "  if (m.y > -1.0 && m.y < 24.0) {\n"
    "    vec2 w = vec2(mod(m.x + 0.5, 27.0) - 0.5, m.y);\n"
    "    if (u_gl.x > 0.5) c = mix(c, vec3(0.0, 0.0, 0.04), 0.85);\n"
    "    float f = texture2D(u_mwall, (w + 0.5) / 32.0).r;\n"
    "    vec3 wc = lvlcol(u_mz.y);\n"
    "    c = mix(c, wc * 0.07, smoothstep(0.45, 0.6, f));\n"
    "    c += wc * (1.0 - smoothstep(0.0, 0.07, abs(f - 0.5))) * 1.1;\n"
    "    vec2 cell = floor(w + 0.5), q = w - cell;\n"
    "    float d = texture2D(u_mdots, (cell + 0.5) / 32.0).r;\n"
    "    if (d > 0.3 && d < 0.7 && length(q) < 0.1) c = vec3(1.0, 0.9, 0.75);\n"
    "    if (d > 0.7 && length(q) < 0.26) c = vec3(1.0, 1.0, 0.6) * (0.85 + 0.15 * sin(u_t * 4.0));\n"
    "    if (abs(cell.y - 11.0) < 0.5 && abs(cell.x - 13.0) < 0.5 && abs(q.y) < 0.08) c = vec3(1.0, 0.6, 0.8);\n"   /* the gate */
    "  }\n"
    "  if (u_fruit.w > 0.5) {\n"
    "    vec2 q = mwrap(m - u_fruit.xy);\n"
    "    if (length(q) < 0.45) {\n"
    "      vec3 fc = 0.5 + 0.5 * cos(6.2832 * (u_fruit.z * 0.13 + vec3(0.0, 0.33, 0.67)));\n"
    "      float sh = mod(u_fruit.z, 3.0);\n"
    "      float in1 = sh < 0.5 ? step(length(q - vec2(0.0, 0.08)), 0.28) : sh < 1.5 ? step(abs(q.x) + abs(q.y - 0.05), 0.33) : step(max(abs(q.x), abs(q.y - 0.05)), 0.24);\n"
    "      if (in1 > 0.5) c = fc * (0.7 + 0.3 * smoothstep(0.3, 0.0, length(q - vec2(-0.08, -0.02))));\n"
    "      if (abs(q.x - 0.04) < 0.035 && q.y < -0.15 && q.y > -0.38) c = vec3(0.3, 0.8, 0.3);\n"
    "    }\n"
    "  }\n"
    "  a = 0.0;\n"
    "  vec3 k;\n"
    "  k = chaser(mwrap(m - u_c0.xy), u_c0, vec3(1.0, 0.22, 0.22), a); if (a > 0.0) { c = k; a = 0.0; }\n"
    "  k = chaser(mwrap(m - u_c1.xy), u_c1, vec3(1.0, 0.35, 0.95), a); if (a > 0.0) { c = k; a = 0.0; }\n"
    "  k = chaser(mwrap(m - u_c2.xy), u_c2, vec3(0.35, 1.0, 0.45), a); if (a > 0.0) { c = k; a = 0.0; }\n"
    "  k = chaser(mwrap(m - u_c3.xy), u_c3, vec3(1.0, 0.6, 0.15), a); if (a > 0.0) { c = k; a = 0.0; }\n"
    "  if (u_mz.w < 0.99) { k = robot(mwrap(m - u_hero.xy), u_hero.z, 1.0, u_mz.w, a); if (a > 0.0) c = k; }\n"
    "  return vec4(c * u_bright, 1.0);\n"
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

/* ---- 6: asteroids: stars fixed to the globe and a faint grid, so you
 * can see it turn as the view follows the ship. u_g.y = flash ---- */
static const char ASTRO[] =
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  vec3 c = vec3(0.0, 0.0, 0.02);\n"
    "  vec3 q = dir * 55.0, f = fract(q) - 0.5;\n"
    "  float h = h3(floor(q));\n"
    "  if (h > 0.985) c += vec3(0.20, 0.22, 0.30) * smoothstep(0.12, 0.0, length(f));\n"   /* faint, small, steady */
    "  float lat = asin(clamp(dir.y, -1.0, 1.0)), lon = atan(dir.x, dir.z);\n"
    "  float g = max(smoothstep(0.996, 1.0, abs(cos(lat * 9.0))), smoothstep(0.996, 1.0, abs(cos(lon * 9.0))));\n"
    "  c += vec3(0.04, 0.07, 0.11) * g;\n"
    "  c += vec3(1.0, 0.95, 0.85) * u_g.y;\n"
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
    RAIN, COPPER, STARS, PLANET, MAZE, ALIENS, ASTRO, PLASMA16, TAPE, FIRE
};
static GLuint g_prog[NSTYLE];
static float  g_t;

/* ---------------------------------------------------------------- */
/* Radial blocks: a falling-block game on rings. x is the sector (it  */
/* wraps all the way round), y the ring, 0 innermost; pieces fall     */
/* inwards. A robot tries every sector and turn and picks the best.   */
/* ---------------------------------------------------------------- */
static float rnd(void) { return (float)rand() / RAND_MAX; }
static float angdiff(float a, float b)
{
    float d = fmodf(a - b + 3.0f * DOME_PI, 2.0f * DOME_PI) - DOME_PI;
    return d;
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

static void draw_booms(void)
{
    for (int i = 0; i < NEXP; i++) {
        Boom *e = &g_ex[i];
        if (!e->on) continue;
        float life = e->big ? 1.4f : 0.7f, f = 1.0f - e->t / life;
        for (int k = 0; k < NPART; k++) {
            float x = e->x + e->vx[k] * e->t, y = e->y + e->vy[k] * e->t;
            const float *c = (k % 3 == 0) ? (const float[3]){ 1.0f, 1.0f, 0.8f } : e->col;
            plate_dot(x, y, c[0], c[1], c[2], f);
        }
        if (e->t < 0.12f)
            for (int k = 0; k < 16; k++)
                plate_dot(e->x + cosf(k * 0.39f) * 0.02f, e->y + sinf(k * 0.39f) * 0.02f, 1, 1, 1, 1);
    }
}

static void hud(int score, int lives, const char *what, int n, int hi, const char *msg, float banner,
                const float *mcol)
{
    char buf[48];
    const float HR = 79.0f, HH = 4.5f;
    snprintf(buf, sizeof buf, "SCORE %06d", score);
    dome_text_arc(&g_pix, buf, HR, -140.0f, HH, 0.9f, 0.9f, 1.0f, 0.95f);
    snprintf(buf, sizeof buf, "LIVES %d", lives < 0 ? 0 : lives);
    dome_text_arc(&g_pix, buf, HR, -90.0f, HH, 1.0f, 0.95f, 0.5f, 0.95f);
    snprintf(buf, sizeof buf, "%s %d", what, n);
    dome_text_arc(&g_pix, buf, HR, -40.0f, HH, 0.6f, 1.0f, 0.6f, 0.95f);
    snprintf(buf, sizeof buf, "HI %06d", hi);
    dome_text_arc(&g_pix, buf, HR, 90.0f, HH, 0.8f, 0.8f, 0.9f, 0.8f);
    if (banner > 0 && msg[0])
        dome_text_arc(&g_pix, msg, 30.0f, 90.0f, 7.0f, mcol[0], mcol[1], mcol[2], fminf(1.0f, banner * 2.0f));
}

/* ---------------------------------------------------------------- */
/* Maze chase: an original maze game. A little round robot clears    */
/* the dots while four spiky chasers hunt it; power cells turn them  */
/* blue so the robot can eat them. The robot stays in the middle of  */
/* the dome and the maze slides round it (the maze repeats left and  */
/* right, so the side tunnel carries straight on). Lives, levels,    */
/* bonus prizes, an extra life at 10,000, and a short cartoon after  */
/* level 4. The robot is played by an expert: it plans a safe path   */
/* to the nearest dot, saves power cells for emergencies and hunts   */
/* blue chasers. All the characters and the maze are made up for    */
/* this program. Drawn in the shader; the board is two tiny textures */
/* ---------------------------------------------------------------- */
#define MW 27
#define MH 24
#define MT 32                      /* texture size */
static const char *MZ[MH] = {
    "###########################",
    "#.........#######.........#",
    "#o###.###.#######.###.###o#",
    "#.###.###.#######.###.###.#",
    "#.........................#",
    "#.###.#######.#######.###.#",
    "#.###.#######.#######.###.#",
    "#.....###.........###.....#",
    "#####.###.###.###.###.#####",
    "#####.###.###.###.###.#####",
    "#####.               .#####",
    "#####.### ###-### ###.#####",
    "#####.### #=====# ###.#####",
    "          #=====#          ",
    "#####.### #=====# ###.#####",
    "#####.### ####### ###.#####",
    "#.....###         ###.....#",
    "#.###.###.###.###.###.###.#",
    "#.###.###.###.###.###.###.#",
    "#............ ............#",
    "#o#######.#######.#######o#",
    "#.#######.#######.#######.#",
    "#.........................#",
    "###########################",
};

enum { MS_HOUSE, MS_LEAVE, MS_ACTIVE, MS_EATEN };
enum { MP_READY, MP_PLAY, MP_DYING, MP_CLEAR, MP_BREAK, MP_OVER };
typedef struct { float x, y; int dir, want; } MEnt;
typedef struct { MEnt e; int state, fright, home_x; float wait; } MChaser;

static const int DX[4] = { 1, 0, -1, 0 }, DY[4] = { 0, 1, 0, -1 };   /* right, down, left, up */
static struct {
    unsigned char dots[MH][MW];      /* 0 none, 1 dot, 2 power cell */
    int left, eaten;                  /* dots left, eaten this level */
    MEnt hero;
    MChaser c[4];
    int phase, level, lives, score, hi, next_life, combo, mode_i;
    float timer, mode_t, fright_t, fruit_t, banner, pop_t, anim, death;
    int fruit_on, fruits, pop_val;
    float pop_x, pop_y;
    char msg[32];
    int dirty;                       /* 2 = upload the whole board, 1 = just the eaten cells */
    int nchg, chg[16][2];
    float glat, glon;                /* planet level: where the view is centred */
} M;
static GLuint g_mwall, g_mdots, g_mdots2;
static int g_mcur;                    /* which of the two dot textures to draw with */

static int m_cell(int x, int y) { x = ((x % MW) + MW) % MW; return (y < 0 || y >= MH) ? '#' : MZ[y][x]; }
static int m_open(int x, int y, int ghost)
{
    int c = m_cell(x, y);
    if (c == '#') return 0;
    if (c == '-' || c == '=') return ghost;
    return 1;
}
static float m_wrapd(float d) { while (d > MW * 0.5f) d -= MW; while (d < -MW * 0.5f) d += MW; return d; }

static void m_board(void)
{
    for (int y = 0; y < MH; y++)
        for (int x = 0; x < MW; x++)
            M.dots[y][x] = MZ[y][x] == '.' ? 1 : MZ[y][x] == 'o' ? 2 : 0;
    M.left = 0;
    for (int y = 0; y < MH; y++) for (int x = 0; x < MW; x++) M.left += M.dots[y][x] != 0;
    M.eaten = 0; M.fruits = 0; M.dirty = 2; M.nchg = 0;
}

static void m_upload(void)
{
    static unsigned char w[MT][MT], d[MT][MT];
    static int walls_done = 0;
    if (!walls_done) {
        memset(w, 0, sizeof w);
        for (int y = 0; y < MH; y++) for (int x = 0; x < MW; x++) w[y][x] = MZ[y][x] == '#' ? 255 : MZ[y][x] == '-' ? 90 : 0;
        glBindTexture(GL_TEXTURE_2D, g_mwall);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, MT, MT, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, w);
        walls_done = 1;
    }
    /* Two dot textures, used in turn: the board is written into the one the
     * last frame did not draw with, so the Pi's GPU never has to stop and
     * wait for a texture it is still using (that wait showed as a stutter
     * and a flash each time a dot was eaten). Done before drawing starts. */
    static int made = 0;
    memset(d, 0, sizeof d);
    for (int y = 0; y < MH; y++) for (int x = 0; x < MW; x++) d[y][x] = M.dots[y][x] == 1 ? 128 : M.dots[y][x] == 2 ? 255 : 0;
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (!made) {
        GLuint t[2] = { g_mdots, g_mdots2 };
        for (int k = 0; k < 2; k++) {
            glBindTexture(GL_TEXTURE_2D, t[k]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, MT, MT, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, d);
        }
        made = 1;
    } else {
        g_mcur ^= 1;
        glBindTexture(GL_TEXTURE_2D, g_mcur ? g_mdots2 : g_mdots);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, MT, MT, GL_LUMINANCE, GL_UNSIGNED_BYTE, d);
    }
    M.nchg = 0;
    M.dirty = 0;
}

static void m_positions(void)
{
    M.hero = (MEnt){ 13, 19, 2, 2 };
    static const int HX[4] = { 13, 11, 13, 15 };
    for (int i = 0; i < 4; i++) {
        M.c[i].e = (MEnt){ (float)HX[i], i == 0 ? 10.0f : 13.0f, i == 0 ? 2 : 3, 0 };
        M.c[i].state = i == 0 ? MS_ACTIVE : MS_HOUSE;
        M.c[i].fright = 0; M.c[i].home_x = HX[i];
        M.c[i].wait = (float)i * 3.5f / (1.0f + 0.15f * (M.level - 1));
    }
    M.mode_i = 0; M.mode_t = 0; M.fright_t = 0; M.fruit_on = 0;
    M.phase = MP_READY; M.timer = 2.0f; snprintf(M.msg, sizeof M.msg, "READY!"); M.banner = 2.0f;
    if (M.level == 5) { M.timer = 3.5f; snprintf(M.msg, sizeof M.msg, "PLANET MAZE: DOUBLE!"); M.banner = 3.5f; }
}

static void m_level(int n)
{
    M.level = n;
    m_board();
    m_positions();
    printf("Maze: level %d (score %d)\n", n, M.score);
}

static void m_new_game(void)
{
    M.lives = 3; M.score = 0; M.next_life = 10000;
    m_level(1);
}

static void m_points(int n)
{
    if (M.level == 5) n *= 2;                 /* the planet level counts double */
    M.score += n;
    if (M.score > M.hi) M.hi = M.score;
    if (M.score >= M.next_life) { M.lives++; M.next_life += 1000000; }   /* one extra life */
}

/* speeds in tiles a second */
static float m_hero_speed(void) { return fminf(7.6f * (1.0f + 0.025f * (M.level - 1)), 9.2f); }
static float m_chaser_speed(const MChaser *c)
{
    if (c->state == MS_EATEN) return 15.0f;
    if (c->state != MS_ACTIVE) return 4.0f;
    if (c->e.y == 13 && (c->e.x < 6 || c->e.x > 20)) return 3.8f;    /* the tunnel */
    if (c->fright) return 4.4f;
    return fminf(7.0f * (1.0f + 0.03f * (M.level - 1)), 8.8f);
}
static float m_fright_time(void) { return fmaxf(1.5f, 7.0f - 0.6f * (M.level - 1)); }

/* breadth-first distances (in tiles) from a cell, through the corridors */
static void m_bfs(int sx, int sy, int ghost, short dist[MH][MW])
{
    static short qx[MW * MH], qy[MW * MH];
    for (int y = 0; y < MH; y++) for (int x = 0; x < MW; x++) dist[y][x] = 9999;
    sx = ((sx % MW) + MW) % MW;
    if (sy < 0 || sy >= MH) return;
    int h = 0, t = 0;
    dist[sy][sx] = 0; qx[t] = (short)sx; qy[t] = (short)sy; t++;
    while (h < t) {
        int x = qx[h], y = qy[h]; h++;
        for (int d = 0; d < 4; d++) {
            int nx = ((x + DX[d]) % MW + MW) % MW, ny = y + DY[d];
            if (!m_open(nx, ny, ghost) || dist[ny][nx] != 9999) continue;
            dist[ny][nx] = dist[y][x] + 1;
            qx[t] = (short)nx; qy[t] = (short)ny; t++;
        }
    }
}

static short g_dexit[MH][MW], g_dhome[MH][MW], g_dtmp[MH][MW];

/* one decision at a cell centre: returns a direction or -1 to stop */
typedef int (*MChoose)(MEnt *e, int who);

static void m_move(MEnt *e, float dist, MChoose choose, int who, int ghost)
{
    int guard = 0;
    while (dist > 1e-5f && guard++ < 20) {
        float cx = roundf(e->x), cy = roundf(e->y);
        if (fabsf(e->x - cx) < 1e-4f && fabsf(e->y - cy) < 1e-4f) {
            e->x = cx; e->y = cy;
            int d = choose(e, who);
            if (d < 0 || !m_open((int)cx + DX[d], (int)cy + DY[d], ghost)) return;
            e->dir = d;
        } else if (who < 0 && e->want == (e->dir + 2) % 4) {
            e->dir = e->want;                         /* the robot may turn back at any time */
        }
        /* distance to the next cell centre along dir */
        float rem;
        if (DX[e->dir] > 0) rem = floorf(e->x) + 1.0f - e->x;
        else if (DX[e->dir] < 0) rem = e->x - ceilf(e->x) + 1.0f;
        else if (DY[e->dir] > 0) rem = floorf(e->y) + 1.0f - e->y;
        else rem = e->y - ceilf(e->y) + 1.0f;
        if (rem > 1.0f) rem -= 1.0f;
        if (rem < 1e-5f) rem = 1.0f;
        float st = fminf(dist, rem);
        e->x += DX[e->dir] * st; e->y += DY[e->dir] * st;
        if (fabsf(e->x - roundf(e->x)) < 1e-4f) e->x = roundf(e->x);
        if (fabsf(e->y - roundf(e->y)) < 1e-4f) e->y = roundf(e->y);
        if (e->x < -0.5f) e->x += MW;
        if (e->x > MW - 0.5f) e->x -= MW;
        dist -= st;
    }
}

static int m_rev(int d) { return (d + 2) % 4; }

static void m_target(int i, int *tx, int *ty)
{
    static const int SX[4] = { 25, 1, 26, 0 }, SY[4] = { -3, -3, 26, 26 };
    MChaser *c = &M.c[i];
    int hx = (int)roundf(M.hero.x), hy = (int)roundf(M.hero.y), hd = M.hero.dir;
    int scatter = (M.mode_i % 2) == 0;
    if (scatter) { *tx = SX[i]; *ty = SY[i]; return; }
    switch (i) {
    case 0: *tx = hx; *ty = hy; break;
    case 1: *tx = hx + 4 * DX[hd]; *ty = hy + 4 * DY[hd]; break;
    case 2: {
        int vx = hx + 2 * DX[hd], vy = hy + 2 * DY[hd];
        *tx = vx + (vx - (int)roundf(M.c[0].e.x)); *ty = vy + (vy - (int)roundf(M.c[0].e.y));
        break; }
    default: {
        float dx = m_wrapd(c->e.x - M.hero.x), dy = c->e.y - M.hero.y;
        if (dx * dx + dy * dy > 64) { *tx = hx; *ty = hy; } else { *tx = SX[3]; *ty = SY[3]; }
        break; }
    }
}

static int m_choose_chaser(MEnt *e, int i)
{
    MChaser *c = &M.c[i];
    int x = (int)e->x, y = (int)e->y;
    if (c->state == MS_LEAVE || c->state == MS_EATEN) {
        short (*dm)[MW] = c->state == MS_LEAVE ? g_dexit : g_dhome;
        if (dm[y][((x % MW) + MW) % MW] == 0) {
            if (c->state == MS_LEAVE) { c->state = MS_ACTIVE; return 2; }
            c->state = MS_LEAVE; c->fright = 0;
            return 3;
        }
        int best = -1, bd = 9999;
        for (int d = 0; d < 4; d++) {
            int nx = ((x + DX[d]) % MW + MW) % MW, ny = y + DY[d];
            if (!m_open(nx, ny, 1)) continue;
            if (dm[ny][nx] < bd) { bd = dm[ny][nx]; best = d; }
        }
        return best;
    }
    if (c->state == MS_HOUSE) return -1;
    int cand[4], n = 0;
    for (int d = 0; d < 4; d++)
        if (d != m_rev(e->dir) && m_open(x + DX[d], y + DY[d], 0)) cand[n++] = d;
    if (n == 0) return m_rev(e->dir);
    if (c->fright) return cand[rand() % n];
    int tx, ty; m_target(i, &tx, &ty);
    int best = cand[0]; float bd = 1e9f;
    static const int ORDER[4] = { 3, 2, 1, 0 };          /* ties: up, left, down, right */
    for (int k = 0; k < 4; k++) {
        int d = ORDER[k], ok = 0;
        for (int j = 0; j < n; j++) ok |= cand[j] == d;
        if (!ok) continue;
        float dx = m_wrapd((float)(x + DX[d] - tx)), dy = (float)(y + DY[d] - ty);
        if (dx * dx + dy * dy < bd) { bd = dx * dx + dy * dy; best = d; }
    }
    return best;
}

/* ---- the expert robot ---------------------------------------------- */
static float g_danger[MH][MW];        /* when the nearest live chaser can get to each cell, s */

/* how soon each live chaser could reach every cell. Chasers cannot turn
 * back, so the search starts from the cell each one is heading into and
 * may not step straight back into the one behind it */
static void m_danger(void)
{
    static short qx[MW * MH], qy[MW * MH];
    for (int y = 0; y < MH; y++) for (int x = 0; x < MW; x++) g_danger[y][x] = 99.0f;
    for (int i = 0; i < 4; i++) {
        MChaser *c = &M.c[i];
        if (c->fright || c->state == MS_EATEN) continue;
        if (c->state == MS_HOUSE && c->wait > 1.5f) continue;
        int ghost = c->state != MS_ACTIVE;
        float sp = fmaxf(m_chaser_speed(c), 6.5f);
        int bx = ((int)roundf(c->e.x) % MW + MW) % MW, by = (int)roundf(c->e.y);
        int ax = bx, ay = by;
        float off = 0.0f;
        int at_centre = fabsf(c->e.x - roundf(c->e.x)) < 1e-3f && fabsf(c->e.y - roundf(c->e.y)) < 1e-3f;
        if (!at_centre && c->state == MS_ACTIVE) {        /* the cell ahead, and how far to it */
            float fx = c->e.x + DX[c->e.dir] * 0.5f, fy = c->e.y + DY[c->e.dir] * 0.5f;
            ax = ((int)roundf(fx) % MW + MW) % MW; ay = (int)roundf(fy);
            if (ax == bx && ay == by) { ax = ((bx + DX[c->e.dir]) % MW + MW) % MW; ay = by + DY[c->e.dir]; }
            off = fabsf(m_wrapd(ax - c->e.x)) + fabsf(ay - c->e.y) - 1.0f;
            bx = ((ax - DX[c->e.dir]) % MW + MW) % MW; by = ay - DY[c->e.dir];
        } else if (c->state == MS_ACTIVE) {
            bx = ((bx - DX[c->e.dir]) % MW + MW) % MW; by = by - DY[c->e.dir];
        } else bx = -100;
        if (ay < 0 || ay >= MH || !m_open(ax, ay, ghost)) continue;
        for (int y = 0; y < MH; y++) for (int x = 0; x < MW; x++) g_dtmp[y][x] = 9999;
        int h = 0, t = 0;
        g_dtmp[ay][ax] = 0; qx[t] = (short)ax; qy[t] = (short)ay; t++;
        if (c->state == MS_HOUSE) off += c->wait * sp;
        while (h < t) {
            int x = qx[h], y = qy[h]; h++;
            for (int d = 0; d < 4; d++) {
                int nx = ((x + DX[d]) % MW + MW) % MW, ny = y + DY[d];
                if (h == 1 && nx == bx && ny == by) continue;     /* no turning straight back */
                if (!m_open(nx, ny, ghost) || g_dtmp[ny][nx] != 9999) continue;
                g_dtmp[ny][nx] = g_dtmp[y][x] + 1;
                qx[t] = (short)nx; qy[t] = (short)ny; t++;
            }
        }
        for (int y = 0; y < MH; y++)
            for (int x = 0; x < MW; x++) {
                float tt = (g_dtmp[y][x] + 1.0f + off - 0.7f) / sp;
                if (tt < g_danger[y][x]) g_danger[y][x] = tt;
            }
    }
}

/* how much safe room there is from a cell, reached after time t0 */
static int m_room(int sx, int sy, float t0, float hs)
{
    static short qx[MW * MH], qy[MW * MH], dist[MH][MW];
    for (int y = 0; y < MH; y++) for (int x = 0; x < MW; x++) dist[y][x] = -1;
    int h = 0, t = 0, n = 0;
    dist[sy][sx] = 0; qx[t] = (short)sx; qy[t] = (short)sy; t++;
    while (h < t && n < 120) {
        int x = qx[h], y = qy[h]; h++;
        n++;
        for (int d = 0; d < 4; d++) {
            int nx = ((x + DX[d]) % MW + MW) % MW, ny = y + DY[d];
            if (!m_open(nx, ny, 0) || dist[ny][nx] >= 0) continue;
            float th = t0 + (dist[y][x] + 1) / hs;
            if (th + 0.2f > g_danger[ny][nx]) continue;
            dist[ny][nx] = dist[y][x] + 1;
            qx[t] = (short)nx; qy[t] = (short)ny; t++;
        }
    }
    return n;
}

static int m_choose_basic(MEnt *e, int who)
{
    (void)who;
    int x = (int)e->x, y = (int)e->y;
    float hs = m_hero_speed();
    m_danger();
    /* safe search: only through cells we reach well before any chaser */
    static short qx[MW * MH], qy[MW * MH], dist[MH][MW];
    static signed char first[MH][MW];
    for (int yy = 0; yy < MH; yy++) for (int xx = 0; xx < MW; xx++) { dist[yy][xx] = -1; first[yy][xx] = -1; }
    int h = 0, t = 0;
    dist[y][x] = 0; qx[t] = (short)x; qy[t] = (short)y; t++;
    int goal = -1, gdist = 9999;
    int want_power = g_danger[y][x] < 1.4f;
    int hunt = M.fright_t > 1.0f;
    while (h < t) {
        int cx = qx[h], cy = qy[h]; h++;
        int d0 = dist[cy][cx];
        if (d0 > 0) {
            int dot = M.dots[cy][cx], is_goal = 0;
            if (dot == 1) is_goal = 1;
            if (dot == 2 && (want_power || M.left <= 4)) is_goal = 1;
            if (M.fruit_on && cx == 13 && cy == 16 && d0 / hs < M.fruit_t - 0.5f) is_goal = 1;
            if (hunt)
                for (int i = 0; i < 4; i++) {
                    MChaser *c = &M.c[i];
                    if (c->state == MS_ACTIVE && c->fright && (int)roundf(c->e.x) == cx && (int)roundf(c->e.y) == cy &&
                        d0 / hs < M.fright_t - 0.5f) is_goal = 2;
                }
            /* a goal only counts if there is room to get away from it */
            if (is_goal && m_room(cx, cy, d0 / hs, hs) < 12) is_goal = 0;
            if (is_goal == 2) { goal = first[cy][cx]; break; }
            if (is_goal && goal < 0) { goal = first[cy][cx]; gdist = d0; }
        }
        if (goal >= 0 && d0 > gdist + 6) break;
        for (int d = 0; d < 4; d++) {
            int nx = ((cx + DX[d]) % MW + MW) % MW, ny = cy + DY[d];
            if (!m_open(nx, ny, 0) || dist[ny][nx] >= 0) continue;
            float th = (d0 + 1) / hs;
            if (th + 0.35f > g_danger[ny][nx]) continue;          /* they would get there first */
            dist[ny][nx] = (short)(d0 + 1);
            first[ny][nx] = (signed char)(d0 == 0 ? d : first[cy][cx]);
            qx[t] = (short)nx; qy[t] = (short)ny; t++;
        }
    }
    if (goal >= 0) return goal;
    /* nothing safe to go for: go where there is the most safe room */
    int best = -1; float bv = -1e9f;
    for (int d = 0; d < 4; d++) {
        int nx = ((x + DX[d]) % MW + MW) % MW, ny = y + DY[d];
        if (!m_open(nx, ny, 0)) continue;
        float v = (float)m_room(nx, ny, 1.0f / hs, hs) + 3.0f * fminf(g_danger[ny][nx], 3.0f);
        if (M.dots[ny][nx] == 2) v += 60.0f;                  /* a power cell saves us */
        if (v > bv) { bv = v; best = d; }
    }
    return best;
}

static int m_tick(float h);
static int g_sim, g_force = -1;
static short g_dotdist[MH][MW];           /* steps to the nearest dot, for the look-ahead */

static void m_dot_field(void)
{
    static short qx[MW * MH], qy[MW * MH];
    int h = 0, t = 0;
    for (int y = 0; y < MH; y++)
        for (int x = 0; x < MW; x++) {
            g_dotdist[y][x] = 9999;
            if (M.dots[y][x]) { g_dotdist[y][x] = 0; qx[t] = (short)x; qy[t] = (short)y; t++; }
        }
    while (h < t) {
        int x = qx[h], y = qy[h]; h++;
        for (int d = 0; d < 4; d++) {
            int nx = ((x + DX[d]) % MW + MW) % MW, ny = y + DY[d];
            if (!m_open(nx, ny, 0) || g_dotdist[ny][nx] != 9999) continue;
            g_dotdist[ny][nx] = g_dotdist[y][x] + 1;
            qx[t] = (short)nx; qy[t] = (short)ny; t++;
        }
    }
}

/* during the look-ahead: head for dots, never straight back, and keep
 * clear of chasers (where they are now, and where they could get to
 * from where they were when the look-ahead began) */
static float g_sim_t;
static int m_choose_sim(MEnt *e)
{
    int x = (int)e->x, y = (int)e->y, best = -1, bd = 1 << 30;
    float hs = m_hero_speed();
    for (int d = 0; d < 4; d++) {
        int nx = ((x + DX[d]) % MW + MW) % MW, ny = y + DY[d];
        if (!m_open(nx, ny, 0)) continue;
        int v = g_dotdist[ny][nx] * 4 + (d == m_rev(e->dir) ? 6 : 0) + (d == e->dir ? 0 : 1);
        if (g_sim_t + 1.0f / hs + 0.25f > g_danger[ny][nx]) v += 400;
        for (int i = 0; i < 4; i++) {
            MChaser *c = &M.c[i];
            if (c->fright || c->state != MS_ACTIVE) continue;
            float dd = fabsf(m_wrapd(c->e.x - nx)) + fabsf(c->e.y - ny);
            if (dd < 2.5f) v += 800;
        }
        if (v < bd) { bd = v; best = d; }
    }
    return best;
}

/* the expert: try each way for a couple of seconds ahead, with the
 * chasers moving as they really would, and take the best */
static int m_choose_hero(MEnt *e, int who)
{
    if (g_force >= 0) { int d = g_force; g_force = -1; return d; }
    if (g_sim) return m_choose_sim(e);
    int plain = m_choose_basic(e, who);
    /* past level 10 it starts to slip now and then, so games do end */
    if (M.level > 10 && rnd() < 0.08f * (M.level - 10)) return plain;
    int x = (int)e->x, y = (int)e->y, best = plain;
    float bv = -1e9f;
    static unsigned char saved[sizeof M];
    memcpy(saved, &M, sizeof M);
    unsigned int seed = (unsigned int)rand();
    m_dot_field();                            /* g_danger is fresh from m_choose_basic */
    for (int d = 0; d < 4; d++) {
        if (!m_open(((x + DX[d]) % MW + MW) % MW, y + DY[d], 0)) continue;
        memcpy(&M, saved, sizeof M);
        srand(seed);
        g_sim = 1; g_force = d;
        int score0 = M.score, died = 0, n;
        for (n = 0; n < 220 && M.phase == MP_PLAY; n++) {
            g_sim_t = n / 60.0f;
            if (m_tick(1.0f / 60.0f)) { died = 1; break; }
        }
        g_sim = 0; g_force = -1;
        float v = died ? -1000.0f + n * 4.0f : (float)(M.score - score0) + (M.phase == MP_CLEAR ? 500.0f : 0.0f);
        if (d == plain) v += 15.0f;                  /* the plan it already had */
        if (v > bv) { bv = v; best = d; }
    }
    memcpy(&M, saved, sizeof M);
    srand(seed);
    return best;
}

static void m_eat(void)
{
    int cx = (int)roundf(M.hero.x), cy = (int)roundf(M.hero.y);
    cx = ((cx % MW) + MW) % MW;
    if (cy < 0 || cy >= MH) return;
    if (fabsf(M.hero.x - roundf(M.hero.x)) > 0.3f || fabsf(M.hero.y - roundf(M.hero.y)) > 0.3f) return;
    int d = M.dots[cy][cx];
    if (!d) return;
    M.dots[cy][cx] = 0; M.left--; M.eaten++;
    if (!g_sim) {
        if (M.nchg < 16) { M.chg[M.nchg][0] = cx; M.chg[M.nchg][1] = cy; }
        M.nchg++;
        if (M.dirty < 1) M.dirty = 1;
    }
    if (d == 1) m_points(10);
    else {
        m_points(50);
        M.fright_t = m_fright_time(); M.combo = 0;
        for (int i = 0; i < 4; i++)
            if (M.c[i].state == MS_ACTIVE) { M.c[i].fright = 1; M.c[i].e.dir = m_rev(M.c[i].e.dir); }
    }
    if (M.eaten == 70 || M.eaten == 150) { M.fruit_on = 1; M.fruit_t = 9.5f; }
}

static const int FRUIT_PTS[8] = { 100, 300, 500, 700, 1000, 2000, 3000, 5000 };

/* one small step of play; returns 1 if the robot was caught */
static int m_tick(float h)
{
    static const float MODE[7] = { 7, 20, 7, 20, 5, 20, 5 };
    if (M.fright_t <= 0 && M.mode_i < 7 && (M.mode_t += h) > MODE[M.mode_i]) {
        M.mode_t = 0; M.mode_i++;
        for (int i = 0; i < 4; i++) if (M.c[i].state == MS_ACTIVE) M.c[i].e.dir = m_rev(M.c[i].e.dir);
    }
    if (M.fright_t > 0 && (M.fright_t -= h) <= 0)
        for (int i = 0; i < 4; i++) M.c[i].fright = 0;
    if (M.fruit_on && (M.fruit_t -= h) <= 0) M.fruit_on = 0;
    /* the robot turns back mid-corridor if a chaser is coming at it */
    if (fabsf(M.hero.x - roundf(M.hero.x)) > 1e-4f || fabsf(M.hero.y - roundf(M.hero.y)) > 1e-4f) {
        M.hero.want = M.hero.dir;
        for (int i = 0; i < 4; i++) {
            MChaser *c = &M.c[i];
            if (c->fright || c->state != MS_ACTIVE) continue;
            float dx = m_wrapd(c->e.x - M.hero.x), dy = c->e.y - M.hero.y;
            float along = dx * DX[M.hero.dir] + dy * DY[M.hero.dir], side = fabsf(dx * DY[M.hero.dir]) + fabsf(dy * DX[M.hero.dir]);
            if (side < 0.2f && along > 0 && along < 2.2f) M.hero.want = m_rev(M.hero.dir);
        }
    }
    m_move(&M.hero, m_hero_speed() * h, m_choose_hero, -1, 0);
    m_eat();
    for (int i = 0; i < 4; i++) {
        MChaser *c = &M.c[i];
        if (c->state == MS_HOUSE) {
            if ((c->wait -= h) <= 0) { c->state = MS_LEAVE; c->e.x = roundf(c->e.x); c->e.y = roundf(c->e.y); }
            continue;
        }
        m_move(&c->e, m_chaser_speed(c) * h, m_choose_chaser, i, c->state != MS_ACTIVE);
        if (c->state != MS_ACTIVE) continue;
        float dx = m_wrapd(c->e.x - M.hero.x), dy = c->e.y - M.hero.y;
        if (fabsf(dx) + fabsf(dy) < 0.7f) {
            if (c->fright) {
                c->state = MS_EATEN; c->fright = 0;
                int pts = 200 << (M.combo < 3 ? M.combo : 3);
                M.combo++; m_points(pts);
                if (!g_sim) { M.pop_val = pts; M.pop_t = 1.0f; M.pop_x = c->e.x; M.pop_y = c->e.y; }
            } else {
                M.phase = MP_DYING; M.timer = 2.4f; M.death = 0;
                return 1;
            }
        }
    }
    if (M.fruit_on && fabsf(m_wrapd(M.hero.x - 13)) + fabsf(M.hero.y - 16) < 0.6f) {
        int k = M.level - 1 < 7 ? M.level - 1 : 7;
        m_points(FRUIT_PTS[k]); M.fruit_on = 0;
        if (!g_sim) { M.pop_val = FRUIT_PTS[k]; M.pop_t = 1.2f; M.pop_x = 13; M.pop_y = 16; }
    }
    if (M.left <= 0) { M.phase = MP_CLEAR; M.timer = 2.2f; }
    return 0;
}

static void maze_step(float dt)
{
    static int started = 0;
    if (!started) {
        started = 1;
        m_bfs(13, 10, 1, g_dexit);
        m_bfs(13, 13, 1, g_dhome);
        m_new_game();
        if (getenv("DOME_MAZE_LEVEL")) m_level(atoi(getenv("DOME_MAZE_LEVEL")));   /* testing */
    }
    M.anim += dt;
    if (M.banner > 0) M.banner -= dt;
    for (int i = 0; i < NEXP; i++)
        if (g_ex[i].on && (g_ex[i].t += dt) > (g_ex[i].big ? 1.4f : 0.7f)) g_ex[i].on = 0;
    if (M.pop_t > 0) M.pop_t -= dt;
    switch (M.phase) {
    case MP_READY: if ((M.timer -= dt) <= 0) M.phase = MP_PLAY; return;
    case MP_OVER: if ((M.timer -= dt) <= 0) m_new_game(); return;
    case MP_CLEAR:
        if ((M.timer -= dt) <= 0) {
            m_level(M.level + 1);
        }
        return;
    case MP_DYING:
        M.death += dt;
        if (M.death > 1.2f && M.death - dt <= 1.2f) {
            static const float C[3] = { 0.4f, 0.9f, 1.0f };
            boom(0, 0, 1, C);
        }
        if ((M.timer -= dt) <= 0) {
            if (--M.lives <= 0) { M.phase = MP_OVER; M.timer = 6.0f; snprintf(M.msg, sizeof M.msg, "GAME OVER"); M.banner = 6.0f;
                                  printf("Maze: game over at level %d, score %d\n", M.level, M.score); }
            else m_positions();
        }
        return;
    default: break;
    }

    float left = dt;
    while (left > 0 && M.phase == MP_PLAY) {
        float h = fminf(left, 1.0f / 120.0f);
        left -= h;
        m_tick(h);
    }
}

static void maze_uniforms(GLuint p)
{
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, g_mwall);
    glUniform1i(glGetUniformLocation(p, "u_mwall"), 1);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, g_mcur ? g_mdots2 : g_mdots);
    glUniform1i(glGetUniformLocation(p, "u_mdots"), 2);
    glActiveTexture(GL_TEXTURE0);
    float death = M.phase == MP_DYING ? fminf(M.death / 1.2f, 1.0f) : M.phase == MP_OVER ? 1.0f : 0.0f;
    float mode = 0.0f;
    glUniform4f(glGetUniformLocation(p, "u_hero"), M.hero.x, M.hero.y, (float)M.hero.dir, M.anim);
    glUniform4f(glGetUniformLocation(p, "u_mz"), 7.0f, (float)M.level, mode, death);
    static const char *CN[4] = { "u_c0", "u_c1", "u_c2", "u_c3" };
    for (int i = 0; i < 4; i++) {
        MChaser *c = &M.c[i];
        float look = c->state == MS_EATEN ? 3.0f : c->fright ? (M.fright_t < 2.0f ? 2.0f : 1.0f) : 0.0f;
        if (M.phase == MP_DYING && M.death > 0.6f) look = 4.0f;
        float y = c->e.y + (c->state == MS_HOUSE ? 0.25f * sinf(M.anim * 5.0f + i) : 0.0f);
        glUniform4f(glGetUniformLocation(p, CN[i]), c->e.x, y, (float)c->e.dir, look);
    }
    glUniform4f(glGetUniformLocation(p, "u_fruit"), 13.0f, 16.0f, (float)(M.level - 1), M.fruit_on ? 1.0f : 0.0f);
    /* the planet level: the view follows the robot round the globe */
    const float TILE = DEG2RAD(5.5f);
    float tl = (11.5f - M.hero.y) * TILE, to = (M.hero.x - 13.0f) * TILE;
    static int was = 0;
    if (!was) { M.glat = tl; M.glon = to; }
    was = M.level == 5;
    M.glat += (tl - M.glat) * 0.08f;
    M.glon += (to - M.glon) * 0.08f;
    glUniform4f(glGetUniformLocation(p, "u_gl"), M.level == 5 ? 1.0f : 0.0f, M.glat, M.glon, TILE);
}

static void maze_draw(void)
{
    static const float YEL[3] = { 1.0f, 0.95f, 0.4f }, RED[3] = { 1.0f, 0.3f, 0.3f };
    dome_points_clear(&g_pix);
    draw_booms();
    char msg[32];
    float ban = M.banner;
    snprintf(msg, sizeof msg, "%s", M.msg);
    if (M.pop_t > 0) { snprintf(msg, sizeof msg, "%d", M.pop_val); ban = M.pop_t; }
    hud(M.score, M.lives, "LEVEL", M.level, M.hi, msg, ban, M.phase == MP_OVER ? RED : YEL);
    dome_points_upload(&g_pix);
}

/* ---------------------------------------------------------------- */
/* Asteroids on a globe: the whole sphere is the playfield, so rocks */
/* and shots go round and come back over the horizon. The view       */
/* follows the ship. Rocks split big, medium, small; saucers turn up */
/* and shoot; lives, score, levels and an extra ship every 10,000.   */
/* Played by a robot that aims ahead, thrusts clear and, in a tight  */
/* spot, jumps to hyperspace. Glowing vector lines made of dots.     */
/* ---------------------------------------------------------------- */
#define ANR 48                     /* rocks */
#define ANB 8                      /* ship's shots */
#define ANU 6                      /* saucer shots */
#define ANP 400                    /* explosion sparks */
#define ANV 11                     /* corners of a rock */
enum { AP_PLAY, AP_DEAD, AP_OVER };

typedef struct { int on, size; float p[3], ax[3], w, rot, spin, shape[ANV]; } ARock;
typedef struct { int on; float p[3], d[3], life; } AShot;
typedef struct { int on; float p[3], d[3], sp, t, life, col[3]; } ASpark;

static struct {
    float p[3], h[3], v[3];         /* ship position, heading, velocity (tangent, rad/s) */
    float cam[9];                    /* rows: right, up, forward (the view centre) */
    ARock r[ANR];
    AShot s[ANB], u[ANU];
    ASpark k[ANP];
    int ufo, ufo_small;
    float up[3], uax[3], ucool, ulife, unext;
    int phase, level, lives, score, hi, next_life;
    float timer, safe, cool, think, hyper, thrust, banner, flash;
    float want[3];
    char msg[32];
} A;
static DomePoints g_ast;

static float dot3(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static void cross3(const float *a, const float *b, float *o)
{
    float x = a[1] * b[2] - a[2] * b[1], y = a[2] * b[0] - a[0] * b[2], z = a[0] * b[1] - a[1] * b[0];
    o[0] = x; o[1] = y; o[2] = z;
}
static void unit3(float *v)
{
    float l = sqrtf(dot3(v, v));
    if (l > 1e-9f) { v[0] /= l; v[1] /= l; v[2] /= l; }
}
/* rotate v about unit axis k by angle a (Rodrigues) */
static void rot3(float *v, const float *k, float a)
{
    float c = cosf(a), s = sinf(a), kv[3], d = dot3(k, v);
    cross3(k, v, kv);
    for (int i = 0; i < 3; i++) v[i] = v[i] * c + kv[i] * s + k[i] * d * (1.0f - c);
}
static void rand_unit(float *v)
{
    do { v[0] = rnd() * 2 - 1; v[1] = rnd() * 2 - 1; v[2] = rnd() * 2 - 1; } while (dot3(v, v) > 1.0f || dot3(v, v) < 0.01f);
    unit3(v);
}
/* the unit tangent at p pointing towards q */
static void toward(const float *p, const float *q, float *o)
{
    float d = dot3(p, q);
    for (int i = 0; i < 3; i++) o[i] = q[i] - d * p[i];
    unit3(o);
}
/* move p (and any tangent vectors with it) along tangent d by angle a */
static void travel(float *p, const float *d, float a, float *t1, float *t2)
{
    float ax[3];
    cross3(p, d, ax); unit3(ax);
    rot3(p, ax, a); unit3(p);
    if (t1) rot3(t1, ax, a);
    if (t2) rot3(t2, ax, a);
}
static float ang3(const float *a, const float *b) { return acosf(fmaxf(-1.0f, fminf(1.0f, dot3(a, b)))); }

static void a_spark(const float *p, int n, float sp, const float *col)
{
    for (int i = 0, made = 0; i < ANP && made < n; i++)
        if (!A.k[i].on) {
            ASpark *k = &A.k[i];
            k->on = 1; memcpy(k->p, p, sizeof k->p);
            float r[3]; rand_unit(r); toward(p, r, k->d);
            /* quick and short-lived, so the bits of a broken rock read as a
             * burst and cannot be taken for tiny rocks */
            k->sp = sp * (1.2f + 1.3f * rnd()); k->t = 0; k->life = 0.22f + 0.30f * rnd();
            memcpy(k->col, col, sizeof k->col);
            made++;
        }
}

static const float ARAD[3] = { 0.27f, 0.18f, 0.09f };    /* large, 2/3, 1/3 */

static void a_rock(int size, const float *p, float speed)
{
    for (int i = 0; i < ANR; i++)
        if (!A.r[i].on) {
            ARock *r = &A.r[i];
            r->on = 1; r->size = size;
            memcpy(r->p, p, sizeof r->p);
            float d[3]; rand_unit(d); toward(p, d, d);
            cross3(p, d, r->ax); unit3(r->ax);
            r->w = speed * (0.7f + 0.6f * rnd());
            r->rot = rnd() * 6.2832f; r->spin = (rnd() - 0.5f) * 2.0f;
            for (int k = 0; k < ANV; k++) r->shape[k] = 0.72f + 0.28f * rnd();
            return;
        }
}

static float a_speed(int size) { return (0.09f + 0.07f * size) * (1.0f + 0.06f * (A.level - 1)); }

static void a_level(int level)
{
    A.level = level;
    memset(A.r, 0, sizeof A.r); memset(A.s, 0, sizeof A.s); memset(A.u, 0, sizeof A.u);
    int n = 4 + level; if (n > 12) n = 12;
    for (int i = 0; i < n; i++) {
        float p[3];
        do rand_unit(p); while (ang3(p, A.p) < 0.6f || ang3(p, A.p) > 1.5f);   /* round us, on our side */
        a_rock(0, p, a_speed(0));
    }
    A.ufo = 0; A.unext = 12.0f + rnd() * 10.0f;
    snprintf(A.msg, sizeof A.msg, "LEVEL %d", level); A.banner = 2.5f;
}

static void a_new_game(void)
{
    A.p[0] = 0; A.p[1] = 0; A.p[2] = 1;
    A.h[0] = 0; A.h[1] = 1; A.h[2] = 0;
    memset(A.v, 0, sizeof A.v);
    float cam[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
    memcpy(A.cam, cam, sizeof cam);
    A.lives = 3; A.score = 0; A.next_life = 10000; A.phase = AP_PLAY; A.safe = 2.0f;
    a_level(1);
}

static void a_points(int n)
{
    A.score += n;
    if (A.score > A.hi) A.hi = A.score;
    if (A.score >= A.next_life) { A.lives++; A.next_life += 10000; }
}

static void a_die(void)
{
    static const float Y[3] = { 1.0f, 0.9f, 0.5f }, W[3] = { 1, 1, 1 };
    a_spark(A.p, 70, 0.35f, Y); a_spark(A.p, 30, 0.2f, W);
    A.phase = AP_DEAD; A.timer = 2.5f; A.flash = 0.25f;
    A.lives--;
}

static void a_break(ARock *r)
{
    static const float RC[3] = { 0.85f, 0.85f, 1.0f };
    static const int PTS[3] = { 20, 50, 100 };
    a_points(PTS[r->size]);
    a_spark(r->p, 30 + 20 * (2 - r->size), 0.30f + 0.10f * (2 - r->size), RC);
    /* copy what we need first: the first piece may reuse this rock's slot */
    int size = r->size;
    float p[3] = { r->p[0], r->p[1], r->p[2] };
    r->on = 0;
    if (size < 2)
        for (int k = 0; k < 2; k++) a_rock(size + 1, p, a_speed(size + 1));
}

/* pick what to shoot at: the closest thing, nearer if it is coming at us */
static int a_target(float *aim, float *dist)
{
    float best = 1e9f; int got = 0;
    for (int i = 0; i < ANR; i++) {
        ARock *r = &A.r[i];
        if (!r->on) continue;
        float d = ang3(A.p, r->p) - ARAD[r->size];
        float fut[3]; memcpy(fut, r->p, sizeof fut);
        float tt = fmaxf(d, 0.0f) / 1.6f;          /* lead: where it will be */
        rot3(fut, r->ax, r->w * tt);
        float d2 = ang3(A.p, fut) - ARAD[r->size];
        float score = d2 - (d2 < d ? 0.15f : 0.0f);
        if (score < best) { best = score; memcpy(aim, fut, sizeof fut); *dist = d; got = 1; }
    }
    if (A.ufo) {
        float d = ang3(A.p, A.up);
        if (d - 0.1f < best) { best = d - 0.1f; memcpy(aim, A.up, sizeof A.up); *dist = d; got = 1; }
    }
    return got;
}

static void asteroids_step(float dt)
{
    static int started = 0;
    if (!started) { started = 1; a_new_game(); }
    if (A.banner > 0) A.banner -= dt;
    if (A.flash > 0) A.flash -= dt;
    if (A.hyper > 0) A.hyper -= dt;

    /* rocks drift round their great circles */
    int left = 0;
    for (int i = 0; i < ANR; i++) {
        ARock *r = &A.r[i];
        if (!r->on) continue;
        left++;
        rot3(r->p, r->ax, r->w * dt); unit3(r->p);
        r->rot += r->spin * dt;
    }
    for (int i = 0; i < ANP; i++) {
        ASpark *k = &A.k[i];
        if (k->on && (k->t += dt) > k->life) k->on = 0;
    }

    if (A.phase == AP_OVER) { if ((A.timer -= dt) <= 0) a_new_game(); return; }
    if (A.phase == AP_DEAD) {
        if ((A.timer -= dt) > 0) goto shots;
        if (A.lives <= 0) { A.phase = AP_OVER; A.timer = 5.0f; snprintf(A.msg, sizeof A.msg, "GAME OVER"); A.banner = 5.0f; return; }
        int clear = 1;                               /* wait until it is safe to come back */
        for (int i = 0; i < ANR; i++) if (A.r[i].on && ang3(A.r[i].p, A.p) < 0.35f) clear = 0;
        if (!clear) goto shots;
        memset(A.v, 0, sizeof A.v);
        A.phase = AP_PLAY; A.safe = 2.0f;
    }

    if (A.phase == AP_PLAY) {
        if (A.safe > 0) A.safe -= dt;
        /* the robot */
        A.think -= dt;
        static float aim[3], adist = 9;
        static int have = 0, flee = 0;
        if (A.think <= 0) {
            A.think = 0.1f;
            have = a_target(aim, &adist);
            flee = 0;
            for (int i = 0; i < ANR; i++) {
                ARock *r = &A.r[i];
                if (!r->on) continue;
                float d = ang3(A.p, r->p) - ARAD[r->size];
                float fut[3]; memcpy(fut, r->p, sizeof fut); rot3(fut, r->ax, r->w * 0.4f);
                float d2 = ang3(A.p, fut) - ARAD[r->size];
                if (d < 0.05f && d2 < d && A.hyper <= 0 && A.safe <= 0) {     /* about to be hit */
                    float np[3]; do rand_unit(np); while (ang3(np, A.p) < 0.6f);
                    float cl = 1;
                    for (int j = 0; j < ANR; j++) if (A.r[j].on && ang3(np, A.r[j].p) < 0.3f) cl = 0;
                    if (cl) {
                        static const float B[3] = { 0.5f, 0.8f, 1.0f };
                        a_spark(A.p, 20, 0.15f, B);
                        float tg[3]; toward(A.p, np, tg);
                        travel(A.p, tg, ang3(A.p, np), A.h, NULL);
                        memset(A.v, 0, sizeof A.v);
                        A.hyper = 4.0f; A.safe = 0.6f;
                        snprintf(A.msg, sizeof A.msg, "HYPERSPACE"); A.banner = 1.0f;
                        have = 0;
                        break;
                    }
                }
                if (d < 0.18f && d2 < d) flee = 1;
            }
        }
        if (have) {
            float t[3]; toward(A.p, aim, t);
            float side[3]; cross3(A.p, A.h, side);
            float c = dot3(t, A.h), s = dot3(t, side), err = atan2f(s, c);
            float turn = fmaxf(-3.5f * dt, fminf(3.5f * dt, err));
            rot3(A.h, A.p, turn); unit3(A.h);
            A.thrust = 0;
            if (fabsf(err) > 2.2f && flee) A.thrust = 1;      /* it is behind us: run */
            if (adist > 0.7f && fabsf(err) < 0.4f) A.thrust = 1;  /* go and find them */
            A.cool -= dt;
            if (fabsf(err) < 0.07f && A.cool <= 0 && adist < 0.85f)
                for (int k = 0; k < ANB; k++)
                    if (!A.s[k].on) {
                        A.s[k].on = 1; memcpy(A.s[k].p, A.p, sizeof A.p); memcpy(A.s[k].d, A.h, sizeof A.h);
                        A.s[k].life = 0.75f; A.cool = 0.22f;
                        break;
                    }
        }
        if (A.thrust) for (int i = 0; i < 3; i++) A.v[i] += A.h[i] * 0.5f * dt;
        float sp = sqrtf(dot3(A.v, A.v));
        float damp = expf(-0.5f * dt);
        if (sp * damp > 0.55f) damp = 0.55f / sp;
        for (int i = 0; i < 3; i++) A.v[i] *= damp;
        sp = sqrtf(dot3(A.v, A.v));
        if (sp > 1e-5f) {
            float d[3] = { A.v[0] / sp, A.v[1] / sp, A.v[2] / sp };
            travel(A.p, d, sp * dt, A.h, A.v);
        }
        /* hit by a rock? */
        if (A.safe <= 0)
            for (int i = 0; i < ANR; i++)
                if (A.r[i].on && ang3(A.p, A.r[i].p) < ARAD[A.r[i].size] * 0.85f + 0.025f) {
                    a_break(&A.r[i]); a_die(); break;
                }
    }

    /* the saucer */
    if (!A.ufo && left > 0 && (A.unext -= dt) <= 0) {
        A.ufo = 1; A.ufo_small = A.score > 8000 && rnd() < 0.6f;
        do rand_unit(A.up); while (ang3(A.up, A.p) < 1.0f || ang3(A.up, A.p) > 1.5f);
        float d[3]; rand_unit(d); toward(A.up, d, d);
        cross3(A.up, d, A.uax); unit3(A.uax);
        A.ulife = 9.0f; A.ucool = 1.5f;
    }
    if (A.ufo) {
        rot3(A.up, A.uax, (A.ufo_small ? 0.32f : 0.22f) * dt); unit3(A.up);
        if ((int)(A.ulife * 2) % 5 == 0) { float j[3]; toward(A.up, A.p, j); cross3(A.up, j, A.uax); unit3(A.uax); }
        if ((A.ulife -= dt) <= 0) { A.ufo = 0; A.unext = 15.0f + rnd() * 15.0f; }
        if ((A.ucool -= dt) <= 0 && A.phase == AP_PLAY) {
            A.ucool = A.ufo_small ? 1.0f : 1.6f;
            for (int k = 0; k < ANU; k++)
                if (!A.u[k].on) {
                    A.u[k].on = 1; memcpy(A.u[k].p, A.up, sizeof A.up);
                    float t[3];
                    if (A.ufo_small) toward(A.up, A.p, t); else { rand_unit(t); toward(A.up, t, t); }
                    memcpy(A.u[k].d, t, sizeof t); A.u[k].life = 1.6f;
                    break;
                }
        }
        if (A.phase == AP_PLAY && A.safe <= 0 && ang3(A.p, A.up) < 0.06f) { A.ufo = 0; a_die(); }
    }

shots:
    for (int k = 0; k < ANB; k++) {
        AShot *s = &A.s[k];
        if (!s->on) continue;
        travel(s->p, s->d, 1.6f * dt, s->d, NULL);
        if ((s->life -= dt) <= 0) { s->on = 0; continue; }
        for (int i = 0; i < ANR; i++)
            if (A.r[i].on && ang3(s->p, A.r[i].p) < ARAD[A.r[i].size]) { a_break(&A.r[i]); s->on = 0; break; }
        if (s->on && A.ufo && ang3(s->p, A.up) < (A.ufo_small ? 0.03f : 0.05f)) {
            static const float G[3] = { 0.5f, 1.0f, 0.6f };
            a_spark(A.up, 40, 0.3f, G);
            a_points(A.ufo_small ? 1000 : 200);
            A.ufo = 0; A.unext = 15.0f + rnd() * 15.0f; s->on = 0;
        }
    }
    for (int k = 0; k < ANU; k++) {
        AShot *s = &A.u[k];
        if (!s->on) continue;
        travel(s->p, s->d, 0.9f * dt, s->d, NULL);
        if ((s->life -= dt) <= 0) { s->on = 0; continue; }
        if (A.phase == AP_PLAY && A.safe <= 0 && ang3(s->p, A.p) < 0.03f) { s->on = 0; a_die(); }
    }

    /* all clear: the next level */
    left = 0;
    for (int i = 0; i < ANR; i++) left += A.r[i].on;
    if (left == 0 && A.phase == AP_PLAY && !A.ufo) a_level(A.level + 1);

    /* the view follows the ship, lagging a little */
    float *fw = &A.cam[6];
    float a = ang3(fw, A.p);
    if (a > 1e-4f) {
        float ax[3]; cross3(fw, A.p, ax); unit3(ax);
        float k = a * (1.0f - expf(-dt * 2.0f));
        for (int r = 0; r < 3; r++) { rot3(&A.cam[r * 3], ax, k); unit3(&A.cam[r * 3]); }
    }
}

/* a world point onto the dome, if it is on the side we can see */
static int a_push(const float *p, const float *c, float al)
{
    if (dot3(p, &A.cam[6]) < -0.02f) return 0;
    dome_points_push(&g_ast, p[0], p[1], p[2], c[0], c[1], c[2], al);
    return 1;
}

static void a_line(const float *a, const float *b, const float *c, float al)
{
    float an = ang3(a, b);
    int n = 1 + (int)(an / 0.0065f);
    if (n > 300) n = 300;
    for (int i = 0; i <= n; i++) {
        float f = (float)i / n, q[3];
        for (int k = 0; k < 3; k++) q[k] = a[k] + (b[k] - a[k]) * f;
        unit3(q);
        a_push(q, c, al);
    }
}

/* a point at offset (x, y) radians in the tangent plane at p */
static void a_off(const float *p, const float *e1, const float *e2, float x, float y, float *o)
{
    for (int k = 0; k < 3; k++) o[k] = p[k] + e1[k] * x + e2[k] * y;
    unit3(o);
}

static void a_frame(const float *p, float *e1, float *e2)
{
    float ref[3] = { 0, 1, 0 };
    if (fabsf(p[1]) > 0.9f) { ref[0] = 1; ref[1] = 0; }
    toward(p, ref, e2);
    cross3(e2, p, e1);
}

static void asteroids_draw(float t)
{
    dome_points_clear(&g_ast);
    static const float RC[3] = { 0.85f, 0.88f, 1.0f }, SH[3] = { 1.0f, 1.0f, 0.75f },
                       FL[3] = { 1.0f, 0.55f, 0.15f }, UC[3] = { 0.5f, 1.0f, 0.6f }, UB[3] = { 1.0f, 0.4f, 0.4f },
                       RED[3] = { 1.0f, 0.3f, 0.3f }, YEL[3] = { 1.0f, 0.95f, 0.4f };
    for (int i = 0; i < ANR; i++) {
        ARock *r = &A.r[i];
        if (!r->on || dot3(r->p, &A.cam[6]) < -0.2f) continue;
        float e1[3], e2[3], prev[3], first[3];
        a_frame(r->p, e1, e2);
        for (int k = 0; k <= ANV; k++) {
            int kk = k % ANV;
            float an = r->rot + kk * 6.2832f / ANV, rr = ARAD[r->size] * r->shape[kk], q[3];
            a_off(r->p, e1, e2, cosf(an) * rr, sinf(an) * rr, q);
            if (k == 0) memcpy(first, q, sizeof q); else a_line(prev, q, RC, 1.0f);
            memcpy(prev, q, sizeof q);
        }
        (void)first;
    }
    /* the ship: a dart, with flame when thrusting; blinks while safe */
    if (A.phase == AP_PLAY && (A.safe <= 0 || ((int)(t * 8.0f) & 1))) {
        float side[3]; cross3(A.p, A.h, side);
        const float L = 0.055f;
        float nose[3], lw[3], rw[3], bk[3];
        a_off(A.p, A.h, side, L, 0, nose);
        a_off(A.p, A.h, side, -L * 0.7f, L * 0.6f, lw);
        a_off(A.p, A.h, side, -L * 0.7f, -L * 0.6f, rw);
        a_off(A.p, A.h, side, -L * 0.4f, 0, bk);
        a_line(nose, lw, SH, 1); a_line(nose, rw, SH, 1);
        a_line(lw, bk, SH, 1); a_line(rw, bk, SH, 1);
        if (A.thrust && ((int)(t * 20.0f) & 1)) {
            float fl[3]; a_off(A.p, A.h, side, -L * 1.3f, 0, fl);
            float a1[3], a2[3];
            a_off(A.p, A.h, side, -L * 0.55f, L * 0.25f, a1);
            a_off(A.p, A.h, side, -L * 0.55f, -L * 0.25f, a2);
            a_line(a1, fl, FL, 1); a_line(a2, fl, FL, 1);
        }
    }
    if (A.ufo) {
        float e1[3], e2[3];
        a_frame(A.up, e1, e2);
        float s = A.ufo_small ? 0.5f : 1.0f, w = 0.06f * s, h = 0.022f * s;
        float p[8][3];
        a_off(A.up, e1, e2, -w, 0, p[0]); a_off(A.up, e1, e2, w, 0, p[1]);
        a_off(A.up, e1, e2, -w * 0.55f, h, p[2]); a_off(A.up, e1, e2, w * 0.55f, h, p[3]);
        a_off(A.up, e1, e2, -w * 0.55f, -h, p[4]); a_off(A.up, e1, e2, w * 0.55f, -h, p[5]);
        a_off(A.up, e1, e2, -w * 0.25f, h * 2, p[6]); a_off(A.up, e1, e2, w * 0.25f, h * 2, p[7]);
        a_line(p[0], p[1], UC, 1); a_line(p[0], p[2], UC, 1); a_line(p[2], p[3], UC, 1); a_line(p[3], p[1], UC, 1);
        a_line(p[0], p[4], UC, 1); a_line(p[4], p[5], UC, 1); a_line(p[5], p[1], UC, 1);
        a_line(p[2], p[6], UC, 1); a_line(p[6], p[7], UC, 1); a_line(p[7], p[3], UC, 1);
    }
    for (int k = 0; k < ANB; k++)
        if (A.s[k].on) {
            float b[3];
            for (int q = 0; q < 3; q++) b[q] = A.s[k].p[q] - A.s[k].d[q] * 0.012f;
            unit3(b);
            a_push(A.s[k].p, SH, 1); a_push(b, SH, 0.6f);
        }
    for (int k = 0; k < ANU; k++)
        if (A.u[k].on) {
            float e1[3], e2[3], q[3];
            a_frame(A.u[k].p, e1, e2);
            for (int m = 0; m < 4; m++) { a_off(A.u[k].p, e1, e2, cosf(m * 1.57f + t * 9) * 0.006f, sinf(m * 1.57f + t * 9) * 0.006f, q); a_push(q, UB, 1); }
        }
    for (int i = 0; i < ANP; i++) {
        ASpark *k = &A.k[i];
        if (!k->on) continue;
        float q[3], d[3];
        memcpy(q, k->p, sizeof q); memcpy(d, k->d, sizeof d);
        travel(q, d, k->sp * k->t, NULL, NULL);
        float fade = 1.0f - k->t / k->life;
        a_push(q, k->col, fade * fade);
    }
    dome_points_upload(&g_ast);

    dome_points_clear(&g_pix);
    hud(A.score, A.lives, "LEVEL", A.level, A.hi, A.msg, A.banner, A.phase == AP_OVER ? RED : YEL);
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
    dome_points_init(&g_scroll, 30000);
    dome_points_init(&g_pix, 60000);
    GLuint *mt[3] = { &g_mwall, &g_mdots, &g_mdots2 };
    for (int k = 0; k < 3; k++) {
        glGenTextures(1, mt[k]);
        glBindTexture(GL_TEXTURE_2D, *mt[k]);
        GLint filt = k == 0 ? GL_LINEAR : GL_NEAREST;     /* walls smooth, so they come out rounded */
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filt);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filt);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    dome_points_init(&g_ast, 40000);
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
    if (s == 4) { maze_step(dt); if (M.dirty) m_upload(); }
    if (s == 5) defender_step(dt);
    if (s == 6) asteroids_step(dt);
}

static void draw(const DomeFrame *f)
{
    int s = style();
    GLuint p = g_prog[s];
    DomeFrame ff = *f;
    mat3 cam;                                  /* asteroids: world -> dome, the ship in front */
    for (int c = 0; c < 3; c++)
        for (int r = 0; r < 3; r++) cam[c * 3 + r] = A.cam[r * 3 + c];
    if (s == 6) memcpy(ff.rot, cam, sizeof cam);
    dome_surface_begin(p, &ff);
    glUniform1f(glGetUniformLocation(p, "u_t"), g_t);
    glUniform1f(glGetUniformLocation(p, "u_scale"), L.scale);
    glUniform1f(glGetUniformLocation(p, "u_bright"), L.bright);
    glUniform1f(glGetUniformLocation(p, "u_grid"), floorf(L.grid + 0.5f));
    if (s == 4) maze_uniforms(p);
    if (s == 6) glUniform4f(glGetUniformLocation(p, "u_g"), 0, A.flash > 0 ? A.flash : 0.0f, 0, 0);
    dome_surface_end();
    mat3 still; m3_identity(still);
    if (s == 5) {
        defender_draw(g_t);
        dome_draw_points(&g_pix, still, f->fade, 5.0f);
    }
    if (s == 4) {
        maze_draw();
        dome_draw_points(&g_pix, still, f->fade, 3.5f);
    }
    if (s == 6) {
        asteroids_draw(g_t);
        dome_draw_points(&g_ast, cam, f->fade * 0.22f, 8.0f);
        dome_draw_points(&g_ast, cam, f->fade, 3.0f);
        dome_draw_points(&g_pix, still, f->fade, 3.5f);
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
