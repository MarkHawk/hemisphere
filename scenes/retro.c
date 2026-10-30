/*
 * retro.c  -  8-bit retro, in chunky pixels. N steps through the styles:
 *
 *   0 digital rain    green character columns pouring down
 *   1 copper bars     rolling colour bars with a sine-wave text scroller
 *   2 rotozoomer      a made-up pixel tile turning and zooming, colours cycling
 *   3 lunar lander    a robot flies a lander down to the pads over jagged
 *                     ground: fuel, bonus pads, crashes, levels
 *   4 maze chase      a robot clears a maze of dots, chased by four spiky
 *                     chasers; it stays in the middle and the maze slides round it
 *   5 rim defender    a ship circling the rim, aliens spiralling out and firing back
 *   6 asteroids       asteroids on a globe: the whole sphere is the playfield
 *   7 16-colour plasma  the demo-scene favourite in a fixed palette
 *   8 tape loading    a home-computer screen loading off tape: pilot tone and
 *                     data stripes in the border, the picture arriving in thirds,
 *                     the colours last, with real attribute clash. A different
 *                     picture each time: built-in people and places, or your
 *                     own photos from the tapes folder
 *   9 pixel fire      the classic demo fire effect
 *
 * All of them face you except asteroids, which follows the ship round the
 * globe. The four games are full games (levels, lives, score, high score), each played by
 * a small robot. scale sets the size of the pixels. The aliens and every picture
 * here are made up for this program; none is copied from a real game.
 */
#include "../dome/dome.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include "../third_party/stb_image.h"    /* the code itself is in planet.c */
#include "../dome/font5x7.h"

#define NSTYLE 10
static const char *NAMES[NSTYLE] = {
    "digital rain", "copper bars", "rotozoomer", "lunar lander", "maze chase",
    "rim defender", "asteroids", "16-colour plasma", "tape loading", "pixel fire"
};

static struct { float style, speed, scale, bright, grid; } L = { 0.0f, 1.0f, 1.0f, 1.0f, 1.0f };

static Param P[] = {
    { "style",  &L.style,  1.0f, 0.0f, 9.0f, 0,
      "0 digital rain, 1 copper bars, 2 rotozoomer, 3 lunar lander, 4 maze chase, 5 rim defender, "
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

/* ---- 2: rotozoomer: a made-up pixel tile, turning and zooming, with
 * its colours cycling through the 16-colour palette ---- */
static const char ROTO[] =
    "float tile(vec2 t){\n"                                         /* one 16x16 tile, made up */
    "  vec2 c = floor(fract(t) * 16.0) - 7.5;\n"
    "  float d = abs(c.x) + abs(c.y);\n"                            /* a diamond gem */
    "  float r = max(abs(c.x), abs(c.y));\n"                        /* and a square frame */
    "  float chk = mod(floor(t.x) + floor(t.y), 2.0);\n"
    "  if (r > 6.9) return 1.0;\n"                                  /* the frame */
    "  if (d < 2.1) return 8.0;\n"                                  /* the gem's sparkle */
    "  if (d < 6.1) return 10.0 + floor(d) + chk * 3.0;\n"
    "  return 2.0 + chk * 11.0 + step(4.5, r);\n"
    "}\n"
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  vec2 p = plane(view), q = pix(p);\n"
    "  float a = u_t * 0.35 + 0.8 * sin(u_t * 0.13);\n"
    "  float z = 1.6 + 1.25 * sin(u_t * 0.41);\n"                   /* zoom in and out */
    "  mat2 R = mat2(cos(a), sin(a), -sin(a), cos(a));\n"
    "  vec2 t = R * q * z + vec2(u_t * 0.37, u_t * 0.21);\n"
    "  float k = tile(t);\n"
    "  float i = k < 1.5 ? 0.0 : k > 7.5 && k < 8.5 ? 8.0 : mod(k + floor(u_t * 6.0), 15.0) + 1.0;\n"
    "  vec3 c = pal16(i);\n"
    "  c *= 0.75 + 0.25 * step(0.5, fract(t.y * 16.0 / 2.0));\n"   /* every other pixel row darker */
    "  return vec4(c * gaps(p) * u_bright, 1.0);\n"
    "}\n";

/* ---- 3: lunar lander. Terrain heights in u_ter (R height, G landing pad
 * bonus / 10, B the nearest pad's middle, A 1 - its bonus / 10 where its
 * label goes), u_ship = x, y, angle, flame;
 * u_g.x = 1 while the wreck burns, u_g.y = 1 on a landing (lights) ---- */
static const char LANDER[] =
    "uniform sampler2D u_ter;\n"
    "uniform vec4 u_ship;\n"
    "float dig(float n, vec2 c){\n"                                 /* 3x5 digits 2-5 and an x */
    "  if (c.x < 0.0 || c.y < 0.0 || c.x > 2.5 || c.y > 4.5) return 0.0;\n"
    "  vec2 f = floor(c);\n"
    "  float rows[5];\n"
    "  if (n < 1.5) { rows[0]=0.0; rows[1]=5.0; rows[2]=2.0; rows[3]=5.0; rows[4]=0.0; }\n"     /* x */
    "  else if (n < 2.5) { rows[0]=7.0; rows[1]=1.0; rows[2]=7.0; rows[3]=4.0; rows[4]=7.0; }\n"
    "  else if (n < 3.5) { rows[0]=7.0; rows[1]=1.0; rows[2]=3.0; rows[3]=1.0; rows[4]=7.0; }\n"
    "  else if (n < 4.5) { rows[0]=5.0; rows[1]=5.0; rows[2]=7.0; rows[3]=1.0; rows[4]=1.0; }\n"
    "  else { rows[0]=7.0; rows[1]=4.0; rows[2]=7.0; rows[3]=1.0; rows[4]=7.0; }\n"
    "  float r = f.y < 0.5 ? rows[0] : f.y < 1.5 ? rows[1] : f.y < 2.5 ? rows[2] : f.y < 3.5 ? rows[3] : rows[4];\n"
    "  return mod(floor(r / pow(2.0, 2.0 - f.x)), 2.0);\n"
    "}\n"
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  vec2 p = plane(view), q = pix(p);\n"
    "  float n = NP();\n"
    "  vec2 g = floor(p * n);\n"
    /* the sky: stars and a small blue planet */
    "  vec3 c = vec3(0.0);\n"
    "  if (h3(vec3(g, 5.0)) > 0.985) c = vec3(0.55, 0.55, 0.7) + 0.3 * step(0.5, fract(u_t * 0.7 + h3(vec3(g, 6.0))));\n"
    "  vec2 ep = q - vec2(-0.32, 0.42);\n"
    "  if (length(ep) < 0.11) {\n"
    "    float land = step(0.3, sin(ep.x * 40.0 + 1.0) + sin(ep.y * 33.0 - ep.x * 20.0));\n"
    "    c = mix(vec3(0.15, 0.35, 0.9), vec3(0.25, 0.75, 0.3), land);\n"
    "    if (ep.x + ep.y * 0.4 > 0.04) c *= 0.35;\n"                 /* the planet's night side */
    "  }\n"
    /* the ground */
    "  vec4 tr = texture2D(u_ter, vec2(clamp(q.x * 0.5 + 0.5, 0.0, 1.0), 0.5));\n"
    "  float h = mix(-0.98, 0.05, tr.r);\n"
    "  float hp = (floor(h * n) + 0.5) / n;\n"
    "  if (q.y <= hp) {\n"
    "    float depth = (hp - q.y) * n;\n"
    "    float cr = h3(vec3(floor(g / 3.0), 9.0));\n"
    "    c = depth < 1.0 ? vec3(0.85, 0.85, 0.8) : mix(vec3(0.42, 0.40, 0.42), vec3(0.28, 0.26, 0.30), step(0.6, cr));\n"
    "    if (depth > 1.0 && h3(vec3(g, 3.0)) > 0.93) c = vec3(0.55, 0.53, 0.55);\n"
    "    if (tr.g > 0.05) {\n"                                         /* a landing pad, and its bonus */
    "      if (depth < 1.0) c = mod(g.x + floor(u_t * 4.0), 2.0) < 1.0 || u_g.y > 0.5 ? vec3(1.0, 0.95, 0.3) : vec3(0.95, 0.4, 0.1);\n"
    "    }\n"
    "    if (tr.a < 0.99 && depth > 1.0) {\n"                         /* the pad's bonus, written below it */
    "      float mid = floor((tr.b * 2.0 - 1.0) * n);\n"
    "      float ph = mix(-0.98, 0.05, texture2D(u_ter, vec2(tr.b, 0.5)).r);\n"
    "      vec2 lc = vec2(g.x - (mid - 3.0), floor(ph * n) - 2.0 - g.y);\n"
    "      float lit = dig(1.0, lc) + dig(floor((1.0 - tr.a) * 10.0 + 0.5), lc - vec2(4.0, 0.0));\n"
    "      if (lit > 0.5) c = vec3(1.0);\n"
    "    }\n"
    "  }\n"
    /* the lander, a chunky sprite turned to its angle */
    "  vec2 d = q - u_ship.xy;\n"
    "  float ca = cos(u_ship.z), sa = sin(u_ship.z);\n"
    "  vec2 l = vec2(ca * d.x + sa * d.y, -sa * d.x + ca * d.y) / 0.055;\n"
    "  if (u_g.x < 0.5 && dot(l, l) < 4.0) {\n"
    "    vec3 sc = vec3(0.0); float on = 0.0;\n"
    "    if (length(l - vec2(0.0, 0.42)) < 0.42) { on = 1.0; sc = vec3(0.85, 0.85, 0.9); }\n"           /* the cabin */
    "    if (length(l - vec2(0.12, 0.5)) < 0.15) sc = vec3(0.2, 0.6, 1.0);\n"                           /* its window */
    "    if (abs(l.x) < 0.55 && l.y > -0.25 && l.y < 0.05) { on = 1.0; sc = vec3(0.95, 0.75, 0.15); }\n" /* the stage */
    "    float leg = abs(abs(l.x) - 0.45 - (-0.2 - l.y) * 0.45);\n"
    "    if (l.y < -0.15 && l.y > -0.72 && leg < 0.1) { on = 1.0; sc = vec3(0.7, 0.7, 0.75); }\n"       /* legs */
    "    if (l.y < -0.66 && l.y > -0.8 && abs(abs(l.x) - 0.7) < 0.16) { on = 1.0; sc = vec3(0.7, 0.7, 0.75); }\n"
    "    float fl = u_ship.w * (0.9 + 0.3 * h3(vec3(floor(u_t * 20.0), 1.0, 1.0)));\n"
    "    if (fl > 0.05 && l.y < -0.25 && l.y > -0.25 - 1.3 * fl && abs(l.x) < 0.22 * (1.0 + (l.y + 0.25) / (1.3 * fl))) {\n"
    "      on = 1.0; sc = (-0.25 - l.y) / (1.3 * fl) < 0.45 ? vec3(1.0, 1.0, 0.6) : vec3(1.0, 0.45, 0.1);\n"
    "    }\n"
    "    if (on > 0.5) c = sc;\n"
    "  }\n"
    "  return vec4(c * gaps(p) * u_bright, 1.0);\n"
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

/* ---- 8: tape loading. The screen is drawn on the CPU (see "the tape
 * loader" below) into u_scr; the border stripes are drawn here.
 * u_tb.x = 0 plain border, 1 pilot tone, 2 data; u_tb.y = stripe phase;
 * u_bc = the plain border colour ---- */
static const char TAPE[] =
    "uniform sampler2D u_scr;\n"
    "uniform vec4 u_tb;\n"
    "uniform vec3 u_bc;\n"
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  vec2 p = plane(view);\n"
    "  vec2 q = p / vec2(0.56, 0.42);\n"                            /* 4:3, like the TV */
    "  vec3 c;\n"
    "  if (abs(q.x) < 1.0 && abs(q.y) < 1.0) {\n"
    "    c = texture2D(u_scr, vec2(q.x * 0.5 + 0.5, 0.5 - q.y * 0.5)).rgb;\n"
    "  } else {\n"
    "    float ln = floor((1.0 - p.y) * 156.0);\n"                 /* TV lines down the dome */
    "    if (u_tb.x > 1.5) {\n"
    "      float band = floor((ln + u_tb.y * 37.0) / 2.0);\n"
    "      c = h3(vec3(band, floor(u_tb.y * 7.0), 3.0)) < 0.5 ? vec3(0.0, 0.0, 0.84) : vec3(0.84, 0.84, 0.0);\n"
    "    } else if (u_tb.x > 0.5) {\n"
    "      c = mod(floor((ln + u_tb.y) / 11.0), 2.0) < 1.0 ? vec3(0.84, 0.0, 0.0) : vec3(0.0, 0.84, 0.84);\n"
    "    } else c = u_bc;\n"
    "  }\n"
    "  return vec4(c * u_bright, 1.0);\n"
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
    RAIN, COPPER, ROTO, LANDER, MAZE, ALIENS, ASTRO, PLASMA16, TAPE, FIRE
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
/* Lunar lander: a robot flies a lander down onto jagged ground. The  */
/* pads pay a bonus (x2 wide, x3, x5 narrow); fuel left over scores   */
/* too. Touch down too fast, tilted or off a pad and it is a crash.   */
/* Each landing is a new level: new ground, more gravity, less fuel.  */
/* The ground and the lander are drawn by the shader (u_ter, u_ship); */
/* the sparks and the score are dots.                                 */
/* ---------------------------------------------------------------- */
#define LN 128                          /* ground columns across x -1..1 */
enum { LD_FLY, LD_DOWN, LD_CRASH, LD_OVER };
static struct {
    float h[LN];                        /* ground height, plane y */
    int   pad[LN];                      /* bonus of the pad here, 0 = none */
    int   label[LN];                    /* bonus written here, under its pad */
    float padmid[LN];
    float x, y, vx, vy, ang, thr, fuel, fuel0, t, banner, skill, err;
    int   state, level, lives, score, hi, target, dirty;
    char  msg[32];
} D;
static GLuint g_ter_tex;
static const float LS = 0.055f;         /* lander size: matches the shader */

static float ld_ground(float x)
{
    float u = (x + 1.0f) * 0.5f * LN;
    int i = (int)u; i = i < 0 ? 0 : i >= LN ? LN - 1 : i;
    return D.h[i];
}

static void ld_terrain(void)
{
    float h[LN + 1];
    h[0] = -0.3f - 0.3f * rnd(); h[LN] = -0.3f - 0.3f * rnd();
    for (int step = LN / 2, amp = 0; step >= 1; step /= 2, amp++)      /* jagged hills */
        for (int i = step; i < LN; i += 2 * step)
            h[i] = 0.5f * (h[i - step] + h[i + step]) + (rnd() - 0.5f) * 0.7f * powf(0.6f, (float)amp);
    for (int i = 0; i < LN; i++) {
        float x = (i + 0.5f) / LN * 2.0f - 1.0f;
        float lo = -sqrtf(fmaxf(0.0f, 1.0f - x * x)) + 0.14f;       /* keep it inside the dome */
        D.h[i] = fminf(fmaxf(h[i], lo), -0.08f);
        D.pad[i] = 0; D.label[i] = 0; D.padmid[i] = 0;
    }
    static const int BONUS[3] = { 2, 3, 5 }, WIDE[3] = { 12, 9, 7 };
    for (int k = 0; k < 3; k++) {
        for (int tries = 0; tries < 50; tries++) {
            int w = WIDE[k], c = 16 + rand() % (LN - 32 - w);
            int ok = 1;
            for (int i = c - 9; i < c + w + 9; i++) if (i >= 0 && i < LN && D.label[i]) ok = 0;
            if (!ok) continue;
            float y = D.h[c + w / 2];
            if (k == 2) for (int i = c; i < c + w; i++) y = fminf(y, D.h[i]);   /* x5 sits low */
            for (int i = c; i < c + w; i++) {
                D.h[i] = y; D.pad[i] = BONUS[k];
            }
            for (int i = c - 4; i < c + w + 4; i++) {           /* room for the label */
                D.label[i] = BONUS[k];
                D.padmid[i] = ((c + w * 0.5f) / LN) * 2.0f - 1.0f;
            }
            break;
        }
    }
    D.dirty = 1;
}

static void ld_upload(void)
{
    if (!g_ter_tex) {
        glGenTextures(1, &g_ter_tex);
        glBindTexture(GL_TEXTURE_2D, g_ter_tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        D.dirty = 1;
    }
    if (!D.dirty) return;
    unsigned char t[LN * 4];
    for (int i = 0; i < LN; i++) {
        float r = (D.h[i] + 0.98f) / 1.03f;
        t[i * 4] = (unsigned char)(fminf(fmaxf(r, 0.0f), 1.0f) * 255.0f + 0.5f);
        t[i * 4 + 1] = (unsigned char)(D.pad[i] * 255 / 10);
        t[i * 4 + 2] = (unsigned char)((D.padmid[i] + 1.0f) * 0.5f * 255.0f + 0.5f);
        t[i * 4 + 3] = (unsigned char)(255 - D.label[i] * 255 / 10);
        /* the shader snaps heights to its pixels, so store them exactly */
        D.h[i] = t[i * 4] / 255.0f * 1.03f - 0.98f;
    }
    glBindTexture(GL_TEXTURE_2D, g_ter_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, LN, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, t);
    D.dirty = 0;
}

/* a fresh lander at the top, aiming for one of the pads */
static void ld_launch(void)
{
    D.x = (rnd() - 0.5f) * 1.0f; D.y = 0.62f;
    D.vx = (rnd() - 0.5f) * 0.12f; D.vy = 0.0f; D.ang = 0.0f; D.thr = 0;
    D.fuel0 = D.fuel = fmaxf(450.0f, 1000.0f - 50.0f * (D.level - 1));
    D.state = LD_FLY; D.t = 0;
    D.skill = 0.8f + 0.5f * rnd();
    D.err = rnd() < 0.12f ? 0.2f + 0.15f * rnd() : 0.0f;   /* now and then it misjudges the height */
    int best = -1; float bv = -1;
    for (int i = 0; i < LN; i++)
        if (D.pad[i] && (i == 0 || D.pad[i - 1] != D.pad[i])) {
            float v = D.pad[i] * (0.6f + 0.8f * rnd()) - fabsf(D.padmid[i] - D.x) * 2.0f;
            if (v > bv) { bv = v; best = i; }
        }
    D.target = best;
}

static void ld_level(int n)
{
    D.level = n;
    ld_terrain();
    ld_launch();
    snprintf(D.msg, sizeof D.msg, "LEVEL %d", n);
    D.banner = 2.0f;
}

static void ld_new_game(void)
{
    D.lives = 3; D.score = 0;
    ld_level(1);
}

static void ld_plate(float px, float py, float *ox, float *oy)   /* flat -> dot coordinates */
{
    float k = 1.0f + px * px + py * py;
    *ox = 2.0f * px / k; *oy = 2.0f * py / k;
}

static void ld_crash(void)
{
    float bx, by;
    ld_plate(D.x, D.y, &bx, &by);
    boom(bx, by, 1, (const float[3]){ 1.0f, 0.75f, 0.2f });
    boom(bx, by, 1, (const float[3]){ 0.9f, 0.9f, 0.95f });
    D.state = LD_CRASH; D.t = 0; D.thr = 0;
    snprintf(D.msg, sizeof D.msg, D.fuel <= 0 ? "OUT OF FUEL" : "CRASHED");
    D.banner = 2.5f;
}

static void lander_step(float dt)
{
    if (!D.level) ld_new_game();
    for (int i = 0; i < NEXP; i++) if (g_ex[i].on && (g_ex[i].t += dt) > (g_ex[i].big ? 1.4f : 0.7f)) g_ex[i].on = 0;
    D.banner -= dt;
    D.t += dt;
    if (D.state == LD_DOWN) { if (D.t > 3.0f) ld_level(D.level + 1); return; }
    if (D.state == LD_CRASH) {
        if (D.t > 2.5f) { if (--D.lives <= 0) { D.state = LD_OVER; D.t = 0; snprintf(D.msg, sizeof D.msg, "GAME OVER"); D.banner = 4; } else ld_launch(); }
        return;
    }
    if (D.state == LD_OVER) { if (D.t > 4.0f) ld_new_game(); return; }

    float g = 0.030f + 0.004f * D.level, T = 0.105f;
    float foot = 0.8f * LS;
    /* the robot pilot: a wanted velocity from where the pad is, then the
     * push that gets it there, then tilt the lander to give that push */
    float xp = D.target >= 0 ? D.padmid[D.target] : 0.0f;
    float hp = D.target >= 0 ? D.h[D.target] : -0.5f;
    float dx = xp - D.x, alt = D.y - foot - hp;
    float vxd = fmaxf(-0.11f, fminf(0.11f, 0.7f * dx * D.skill));
    float vyd;
    if (fabsf(dx) > 0.025f) {
        float top = -1.0f;                                         /* clear the hills on the way */
        float a = fminf(D.x, xp), b = fmaxf(D.x, xp);
        for (float x = a - 0.06f; x <= b + 0.06f; x += 0.01f) top = fmaxf(top, ld_ground(x));
        vyd = D.y - foot < top + 0.12f ? 0.05f : (D.y - foot > top + 0.25f ? -0.05f : 0.0f);
    } else vyd = -fminf(0.12f, 0.022f + 0.3f * (alt + D.err) * D.skill);
    float ax = 2.2f * (vxd - D.vx), ay = 2.2f * (vyd - D.vy) + g;
    if (ay < 0.0f) ay = 0.0f;
    float want = atan2f(-ax, ay);
    float lim = alt < 0.06f ? 0.05f : 1.1f;
    want = fmaxf(-lim, fminf(lim, want));
    float da = want - D.ang, turn = 2.4f * dt;
    D.ang += fmaxf(-turn, fminf(turn, da));
    float need = sqrtf(ax * ax + ay * ay) * cosf(D.ang - want) / T;
    D.thr = D.fuel > 0 ? fmaxf(0.0f, fminf(1.0f, need)) : 0.0f;
    D.fuel = fmaxf(0.0f, D.fuel - D.thr * 55.0f * dt);
    D.vx += -sinf(D.ang) * T * D.thr * dt;
    D.vy += (cosf(D.ang) * T * D.thr - g) * dt;
    D.x += D.vx * dt; D.y += D.vy * dt;
    if (D.x < -0.97f || D.x > 0.97f || D.y > 0.95f) { D.vx = -D.vx * 0.5f; D.x = fmaxf(-0.97f, fminf(0.97f, D.x)); }

    /* touchdown: both feet */
    float fx = 0.7f * LS, gl = ld_ground(D.x - fx), gr = ld_ground(D.x + fx), gm = ld_ground(D.x);
    float low = D.y - foot;
    if (low <= fmaxf(fmaxf(gl, gr), gm)) {
        int i0 = (int)((D.x - fx + 1.0f) * 0.5f * LN), i1 = (int)((D.x + fx + 1.0f) * 0.5f * LN);
        i0 = i0 < 0 ? 0 : i0 >= LN ? LN - 1 : i0; i1 = i1 < 0 ? 0 : i1 >= LN ? LN - 1 : i1;
        int pad = D.pad[i0] && D.pad[i0] == D.pad[i1] ? D.pad[i0] : 0;
        if (pad && D.vy > -0.075f && fabsf(D.vx) < 0.05f && fabsf(D.ang) < 0.2f) {
            int pts = 50 * pad + (int)D.fuel / 2;
            D.score += pts;
            if (D.score > D.hi) D.hi = D.score;
            D.y = D.h[i0] + foot; D.vx = D.vy = 0; D.thr = 0;
            D.state = LD_DOWN; D.t = 0;
            snprintf(D.msg, sizeof D.msg, "LANDED x%d  %d", pad, pts);
            D.banner = 3.0f;
        } else ld_crash();
    }
}

static void lander_uniforms(GLuint p)
{
    glActiveTexture(GL_TEXTURE1);
    ld_upload();
    glBindTexture(GL_TEXTURE_2D, g_ter_tex);
    glUniform1i(glGetUniformLocation(p, "u_ter"), 1);
    glActiveTexture(GL_TEXTURE0);
    glUniform4f(glGetUniformLocation(p, "u_ship"), D.x, D.y, D.ang, D.thr);
    glUniform4f(glGetUniformLocation(p, "u_g"), D.state == LD_CRASH || D.state == LD_OVER ? 1.0f : 0.0f,
                D.state == LD_DOWN ? 1.0f : 0.0f, 0, 0);
}

static void lander_draw(void)
{
    dome_points_clear(&g_pix);
    draw_booms();
    hud(D.score, D.lives, "LEVEL", D.level, D.hi, D.msg, D.banner, (const float[3]){ 1.0f, 0.9f, 0.4f });
    char buf[40];
    snprintf(buf, sizeof buf, "FUEL %04d", (int)D.fuel);
    float fc = D.fuel < D.fuel0 * 0.2f ? 1.0f : 0.6f;
    dome_text_arc(&g_pix, buf, 79.0f, 140.0f, 4.5f, 1.0f, fc, fc * 0.5f, 0.95f);
    snprintf(buf, sizeof buf, "V %03d H %03d", (int)fabsf(D.vy * 1000.0f), (int)fabsf(D.vx * 1000.0f));
    dome_text_arc(&g_pix, buf, 79.0f, 40.0f, 4.5f, 0.7f, 0.9f, 1.0f, 0.95f);
    dome_points_upload(&g_pix);
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

/* ================================================================ */
/* the tape loader (style 8)                                         */
/* ================================================================ */
/* ---- tape loading: a Spectrum-style screen loader --------------------
 * The picture is drawn (or read from a photo) into a 256x192 RGB canvas,
 * then converted the way a real 8-bit screen works: one bit per pixel,
 * plus one ink and one paper colour (from 15) for each 8x8 cell, so the
 * colours clash at cell edges. It then "loads" in the real order: the
 * bitmap in thirds, a pixel line of every character row in turn, and the
 * colours last. */

#define TW 256
#define TH 192
static float g_cv[TH][TW][3];          /* canvas, 0..1 RGB */
static unsigned short g_cid[TH][TW];   /* shape ids, for black outlines */
static unsigned short g_id;
static int g_outline;
static unsigned int g_seed = 1;
static float trnd(void) { g_seed = g_seed * 1664525u + 1013904223u; return (g_seed >> 8) / 16777216.0f; }
static float hsh(int x, int y, int k)
{
    unsigned int h = (unsigned)x * 374761393u + (unsigned)y * 668265263u + (unsigned)k * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return ((h ^ (h >> 16)) & 0xffffff) / 16777216.0f;
}

static void px(int x, int y, const float c[3], float a)
{
    if (x < 0 || y < 0 || x >= TW || y >= TH) return;
    for (int k = 0; k < 3; k++) g_cv[y][x][k] += (c[k] - g_cv[y][x][k]) * a;
    if (a > 0.5f) g_cid[y][x] = g_outline ? g_id : 0;
}
#define C3(r, g, b) ((const float[3]){ (r), (g), (b) })
static void shape(int outline) { g_outline = outline; g_id++; }

static void vgrad(int y0, int y1, const float a[3], const float b[3])
{
    shape(0);
    for (int y = y0; y < y1; y++) {
        float t = (float)(y - y0) / (float)(y1 - y0 > 1 ? y1 - y0 - 1 : 1);
        float c[3] = { a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t };
        for (int x = 0; x < TW; x++) px(x, y, c, 1.0f);
    }
}
static void rect(float x0, float y0, float x1, float y1, const float c[3])
{
    for (int y = (int)y0; y < (int)y1; y++)
        for (int x = (int)x0; x < (int)x1; x++) px(x, y, c, 1.0f);
}
static void disc(float cx, float cy, float r, const float c[3], float a)
{
    for (int y = (int)(cy - r - 1); y <= (int)(cy + r + 1); y++)
        for (int x = (int)(cx - r - 1); x <= (int)(cx + r + 1); x++) {
            float d = sqrtf((x + 0.5f - cx) * (x + 0.5f - cx) + (y + 0.5f - cy) * (y + 0.5f - cy));
            if (d < r) px(x, y, c, a);
        }
}
static void ellipse(float cx, float cy, float rx, float ry, const float c[3])
{
    for (int y = (int)(cy - ry - 1); y <= (int)(cy + ry + 1); y++)
        for (int x = (int)(cx - rx - 1); x <= (int)(cx + rx + 1); x++) {
            float u = (x + 0.5f - cx) / rx, v = (y + 0.5f - cy) / ry;
            if (u * u + v * v < 1.0f) px(x, y, c, 1.0f);
        }
}
static void glow(float cx, float cy, float r, const float c[3], float a)
{
    for (int y = (int)(cy - r); y <= (int)(cy + r); y++)
        for (int x = (int)(cx - r); x <= (int)(cx + r); x++) {
            float d = sqrtf((x - cx) * (x - cx) + (y - cy) * (y - cy)) / r;
            if (d < 1.0f) {
                if (x < 0 || y < 0 || x >= TW || y >= TH) continue;
                float k = (1.0f - d) * (1.0f - d) * a;
                for (int i = 0; i < 3; i++) g_cv[y][x][i] = fminf(1.0f, g_cv[y][x][i] + c[i] * k);
            }
        }
}
/* filled polygon, points as x,y pairs */
static void poly(int n, const float *p, const float c[3])
{
    float y0 = 1e9f, y1 = -1e9f;
    for (int i = 0; i < n; i++) { y0 = fminf(y0, p[2 * i + 1]); y1 = fmaxf(y1, p[2 * i + 1]); }
    for (int y = (int)floorf(y0); y <= (int)ceilf(y1); y++) {
        float yc = y + 0.5f, xs[32]; int k = 0;
        for (int i = 0; i < n && k < 32; i++) {
            float ax = p[2 * i], ay = p[2 * i + 1], bx = p[2 * ((i + 1) % n)], by = p[2 * ((i + 1) % n) + 1];
            if ((ay <= yc && by > yc) || (by <= yc && ay > yc)) xs[k++] = ax + (yc - ay) / (by - ay) * (bx - ax);
        }
        for (int i = 1; i < k; i++) for (int j = i; j > 0 && xs[j - 1] > xs[j]; j--) { float t = xs[j]; xs[j] = xs[j - 1]; xs[j - 1] = t; }
        for (int i = 0; i + 1 < k; i += 2)
            for (int x = (int)ceilf(xs[i] - 0.5f); x < (int)ceilf(xs[i + 1] - 0.5f); x++) px(x, y, c, 1.0f);
    }
}
static void tri(float ax, float ay, float bx, float by, float cx, float cy, const float c[3])
{ float p[6] = { ax, ay, bx, by, cx, cy }; poly(3, p, c); }
static void quad(float ax, float ay, float bx, float by, float cx, float cy, float dx, float dy, const float c[3])
{ float p[8] = { ax, ay, bx, by, cx, cy, dx, dy }; poly(4, p, c); }
static void line(float x0, float y0, float x1, float y1, float w, const float c[3])
{
    float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy);
    if (l < 1e-3f) return;
    float nx = -dy / l * w * 0.5f, ny = dx / l * w * 0.5f;
    quad(x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, c);
}
/* a ridge line filled down to the bottom: heights from a sum of sines */
static void ridge(float base, float amp, float f1, float f2, float ph, float jag, const float c[3])
{
    for (int x = 0; x < TW; x++) {
        float h = base - amp * (0.6f * sinf(x * f1 + ph) + 0.4f * sinf(x * f2 + ph * 2.3f))
                - jag * (hsh(x / 3, 0, (int)(ph * 100)) - 0.5f);
        for (int y = (int)h; y < TH; y++) px(x, y, c, 1.0f);
    }
}
static void stars(int n, int ymax)
{
    for (int i = 0; i < n; i++) {
        int x = (int)(trnd() * TW), y = (int)(trnd() * ymax);
        float b = 0.6f + 0.4f * trnd();
        px(x, y, C3(b, b, b), 1.0f);
        if (trnd() < 0.12f) { px(x + 1, y, C3(b, b, b), 1.0f); px(x - 1, y, C3(b, b, b), 1.0f);
                              px(x, y + 1, C3(b, b, b), 1.0f); px(x, y - 1, C3(b, b, b), 1.0f); }
    }
}
static void outline_pass(void)
{
    static unsigned short id[TH][TW];
    memcpy(id, g_cid, sizeof id);
    for (int y = 0; y < TH; y++)
        for (int x = 0; x < TW; x++) {
            unsigned short a = id[y][x];
            unsigned short r = x + 1 < TW ? id[y][x + 1] : a, d = y + 1 < TH ? id[y + 1][x] : a;
            unsigned short l = x > 0 ? id[y][x - 1] : a, u = y > 0 ? id[y - 1][x] : a;
            /* the outline goes on the outside of the nearer (higher id) shape */
            if (a < r || a < d || a < l || a < u) {
                unsigned short m = r; if (d > m) m = d; if (l > m) m = l; if (u > m) m = u;
                if (m && m > a) { g_cv[y][x][0] = g_cv[y][x][1] = g_cv[y][x][2] = 0.0f; }
            }
        }
}

/* ---- the pictures: all made up for this program ---- */
static void pic_lighthouse(void)
{
    vgrad(0, 120, C3(0.02f, 0.02f, 0.25f), C3(0.6f, 0.1f, 0.65f));
    stars(70, 90);
    float mx = 40 + trnd() * 60;
    shape(1); disc(mx, 32, 14, C3(1, 1, 0.85f), 1);
    shape(0); disc(mx + 6, 28, 12, C3(0.07f, 0.05f, 0.3f), 1);
    vgrad(120, 192, C3(0.05f, 0.1f, 0.55f), C3(0.0f, 0.0f, 0.2f));
    for (int i = 0; i < 40; i++) {                       /* moonlight on the waves */
        float y = 124 + i * 1.7f, w = 3 + trnd() * (16 - i * 0.25f);
        rect(mx - w + trnd() * 6, y, mx + w, y + 1, C3(0.9f, 0.9f, 0.6f));
    }
    shape(1);
    float rk[] = { 150, 192, 160, 150, 175, 132, 205, 128, 230, 138, 256, 130, 256, 192 };
    poly(7, rk, C3(0.25f, 0.2f, 0.2f));
    shape(1); quad(190, 132, 214, 132, 208, 52, 196, 52, C3(0.95f, 0.95f, 0.95f));
    for (int b = 0; b < 3; b++) {
        float y0 = 60 + b * 26, y1 = y0 + 11;
        float x0 = 196 - (y0 - 52) * 0.075f, x1 = 208 + (y0 - 52) * 0.075f;
        float x2 = 196 - (y1 - 52) * 0.075f, x3 = 208 + (y1 - 52) * 0.075f;
        quad(x0, y0, x1, y0, x3, y1, x2, y1, C3(0.9f, 0.05f, 0.05f));
    }
    shape(1); rect(192, 48, 212, 52, C3(0.1f, 0.1f, 0.1f));
    shape(1); rect(195, 36, 209, 48, C3(1, 0.95f, 0.3f));
    shape(1); tri(192, 36, 212, 36, 202, 26, C3(0.9f, 0.05f, 0.05f));
    shape(0);
    tri(196, 40, 0, 10, 0, 70, C3(1, 1, 0.5f));          /* the beam */
    glow(202, 42, 30, C3(1, 1, 0.6f), 0.9f);
}
static void pic_castle(void)
{
    vgrad(0, 150, C3(0.35f, 0.05f, 0.45f), C3(1.0f, 0.55f, 0.1f));
    float sx = 60 + trnd() * 140;
    shape(0); disc(sx, 112, 30, C3(1, 0.95f, 0.3f), 1);
    for (int i = 0; i < 5; i++) rect(sx - 32, 96 + i * 7, sx + 32, 98 + i * 7, C3(1.0f, 0.5f, 0.15f));
    shape(1); ridge(128, 10, 0.02f, 0.05f, trnd() * 6, 0, C3(0.1f, 0.4f, 0.1f));
    float cx = 128;
    shape(1); rect(cx - 50, 70, cx + 50, 140, C3(0.55f, 0.5f, 0.55f));
    for (int i = 0; i < 9; i++) rect(cx - 50 + i * 12, 64, cx - 44 + i * 12, 70, C3(0.55f, 0.5f, 0.55f));
    float tw[3] = { cx - 62, cx - 12, cx + 38 }, th[3] = { 48, 30, 48 };
    for (int t = 0; t < 3; t++) {
        shape(1); rect(tw[t], th[t], tw[t] + 24, 140, C3(0.65f, 0.6f, 0.65f));
        shape(1); tri(tw[t] - 4, th[t], tw[t] + 28, th[t], tw[t] + 12, th[t] - 26, C3(0.2f, 0.25f, 0.8f));
        shape(1); line(tw[t] + 12, th[t] - 26, tw[t] + 12, th[t] - 40, 1.5f, C3(0.1f, 0.1f, 0.1f));
        tri(tw[t] + 13, th[t] - 40, tw[t] + 24, th[t] - 36, tw[t] + 13, th[t] - 32, C3(1, 0.1f, 0.1f));
        shape(1); rect(tw[t] + 9, th[t] + 12, tw[t] + 15, th[t] + 22, C3(1, 0.9f, 0.2f));
    }
    shape(1); rect(cx - 9, 112, cx + 9, 140, C3(0.15f, 0.08f, 0.05f));
    disc(cx, 112, 9, C3(0.15f, 0.08f, 0.05f), 1);
    shape(1); ridge(178, 8, 0.03f, 0.07f, trnd() * 6, 0, C3(0.0f, 0.55f, 0.0f));
    shape(1);
    float pth[] = { cx - 9, 140, cx + 9, 140, cx + 40, 192, cx - 40, 192 };
    poly(4, pth, C3(0.85f, 0.75f, 0.4f));
    for (int i = 0; i < 6; i++) {                                 /* birds */
        float bx = 20 + trnd() * 220, by = 10 + trnd() * 40;
        shape(0); line(bx - 4, by - 2, bx, by, 1, C3(0, 0, 0)); line(bx, by, bx + 4, by - 2, 1, C3(0, 0, 0));
    }
}
static void pic_city(void)
{
    vgrad(0, 140, C3(0.0f, 0.0f, 0.15f), C3(0.3f, 0.05f, 0.4f));
    stars(50, 70);
    shape(1); disc(200 + trnd() * 30, 30, 12, C3(1, 1, 0.9f), 1);
    float x = 0;
    while (x < TW) {
        float w = 14 + trnd() * 22, h = 40 + trnd() * 70;
        float c = 0.15f + trnd() * 0.25f;
        shape(1); rect(x, 140 - h, x + w, 140, C3(c * 0.6f, c * 0.6f, c));
        for (float wy = 140 - h + 4; wy < 136; wy += 6)
            for (float wx = x + 3; wx < x + w - 3; wx += 5)
                if (trnd() < 0.45f) rect(wx, wy, wx + 2, wy + 3, trnd() < 0.8f ? C3(1, 0.9f, 0.3f) : C3(0.9f, 0.95f, 1));
        if (trnd() < 0.25f) { shape(1); line(x + w * 0.5f, 140 - h, x + w * 0.5f, 140 - h - 18, 1.5f, C3(0.6f, 0.6f, 0.6f));
                              shape(0); disc(x + w * 0.5f, 140 - h - 19, 1.5f, C3(1, 0, 0), 1); }
        x += w + 1;
    }
    vgrad(140, 192, C3(0.05f, 0.05f, 0.35f), C3(0.0f, 0.0f, 0.1f));
    for (int i = 0; i < 160; i++) {                     /* reflections of the windows */
        float rx = trnd() * TW, ry = 142 + trnd() * 48;
        rect(rx, ry, rx + 2 + trnd() * 5, ry + 1, C3(1, 0.85f, 0.3f));
    }
    shape(1); rect(0, 138, TW, 142, C3(0.3f, 0.3f, 0.3f));
}
static void pic_desert(void)
{
    vgrad(0, 120, C3(0.1f, 0.5f, 0.95f), C3(0.75f, 0.95f, 1.0f));
    float sx = 30 + trnd() * 60;
    shape(0); glow(sx, 30, 40, C3(1, 1, 0.6f), 0.8f);
    shape(1); disc(sx, 30, 13, C3(1, 1, 0.4f), 1);
    shape(1); ridge(122, 6, 0.015f, 0.04f, trnd() * 6, 0, C3(0.95f, 0.75f, 0.3f));
    float px0 = 110 + trnd() * 30;
    float base[3] = { px0, px0 + 70, px0 - 60 }, hw[3] = { 45, 30, 22 };
    for (int i = 2; i >= 0; i--) {
        float b = base[i], w = hw[i], top = 122 - w * 1.2f;
        shape(1); tri(b - w, 124, b, top, b + w * 0.35f, 124, C3(1.0f, 0.85f, 0.35f));
        shape(1); tri(b, top, b + w, 124, b + w * 0.35f, 124, C3(0.7f, 0.35f, 0.1f));
    }
    shape(1); ridge(150, 10, 0.025f, 0.06f, trnd() * 6, 0, C3(1.0f, 0.8f, 0.35f));
    shape(1); ridge(175, 9, 0.02f, 0.05f, trnd() * 6, 0, C3(0.85f, 0.55f, 0.2f));
    float tx = 215 + trnd() * 20;                            /* a palm */
    shape(1); quad(tx - 3, 172, tx + 3, 172, tx + 9, 108, tx + 5, 108, C3(0.55f, 0.3f, 0.1f));
    for (int k = 0; k < 6; k++) {
        float a = -2.6f + k * 0.55f;
        shape(1); quad(tx + 7, 108, tx + 7 + cosf(a) * 30, 108 + sinf(a) * 14 + 10, tx + 7 + cosf(a + 0.1f) * 24,
                       108 + sinf(a + 0.1f) * 10 + 8, tx + 7, 104, C3(0.0f, 0.65f, 0.1f));
    }
    for (int c = 0; c < 4; c++) {                            /* a caravan on the dunes */
        float cx = 20 + c * 14, cy = 146 + c * 0.5f;
        shape(1); ellipse(cx, cy, 5, 3, C3(0.45f, 0.25f, 0.05f));
        line(cx + 4, cy - 1, cx + 7, cy - 6, 1.5f, C3(0.45f, 0.25f, 0.05f));
        line(cx - 3, cy + 2, cx - 3, cy + 7, 1, C3(0.45f, 0.25f, 0.05f));
        line(cx + 3, cy + 2, cx + 3, cy + 7, 1, C3(0.45f, 0.25f, 0.05f));
    }
}
static void pic_mountains(void)
{
    vgrad(0, 110, C3(0.1f, 0.35f, 0.9f), C3(0.6f, 0.85f, 1.0f));
    for (int i = 0; i < 4; i++) {
        float cx = trnd() * TW, cy = 15 + trnd() * 30;
        shape(0); for (int k = 0; k < 4; k++) disc(cx + k * 8 - 12, cy + (k & 1) * 2, 7 + (k == 1) * 3, C3(1, 1, 1), 1);
    }
    float ph = trnd() * 6;
    for (int m = 0; m < 3; m++) {
        float peak = 30 + m * 60 + trnd() * 30, h = 40 + trnd() * 20, w = 70;
        float top = 110 - h - 30;
        shape(1); tri(peak - w, 112, peak, top, peak + w, 112, C3(0.35f, 0.35f, 0.55f));
        tri(peak, top, peak + w, 112, peak + w * 0.3f, 112, C3(0.25f, 0.25f, 0.4f));
        shape(0);
        float s = 0.3f;
        float sn[] = { peak - w * s, top + (112 - top) * s, peak, top, peak + w * s, top + (112 - top) * s,
                       peak + w * s * 0.5f, top + (112 - top) * s * 0.75f, peak, top + (112 - top) * s * 1.1f,
                       peak - w * s * 0.5f, top + (112 - top) * s * 0.8f };
        poly(6, sn, C3(1, 1, 1));
    }
    (void)ph;
    vgrad(112, 150, C3(0.1f, 0.4f, 0.8f), C3(0.05f, 0.2f, 0.6f));
    for (int y = 112; y < 150; y++)                          /* the peaks in the lake */
        for (int x = 0; x < TW; x++) {
            int sy = 112 - (y - 112) * 2;
            if (sy >= 0 && (y & 1) && hsh(x / 4, y, 9) > 0.3f)
                for (int k = 0; k < 3; k++) g_cv[y][x][k] = g_cv[y][x][k] * 0.4f + g_cv[sy][x][k] * 0.5f;
        }
    shape(1); ridge(160, 6, 0.03f, 0.08f, trnd() * 6, 0, C3(0.1f, 0.5f, 0.1f));
    for (int i = 0; i < 14; i++) {                           /* pines */
        float tx = trnd() * TW, ty = 165 + trnd() * 25, h = 26 + trnd() * 22;
        shape(1); rect(tx - 1.5f, ty - 4, tx + 1.5f, ty + 3, C3(0.4f, 0.2f, 0.05f));
        for (int k = 0; k < 3; k++) {
            float y0 = ty - 4 - k * h * 0.28f;
            tri(tx - 10 + k * 2.5f, y0, tx + 10 - k * 2.5f, y0, tx, y0 - h * 0.45f, C3(0.0f, 0.4f + 0.1f * k, 0.1f));
        }
    }
}
static void pic_island(void)
{
    vgrad(0, 110, C3(0.0f, 0.55f, 1.0f), C3(0.85f, 1.0f, 1.0f));
    float sx = 180 + trnd() * 50;
    shape(1); disc(sx, 28, 14, C3(1, 1, 0.3f), 1);
    vgrad(110, 192, C3(0.0f, 0.75f, 0.85f), C3(0.0f, 0.3f, 0.7f));
    for (int i = 0; i < 70; i++) {
        float wx = trnd() * TW, wy = 112 + trnd() * 78;
        shape(0); rect(wx, wy, wx + 4 + (wy - 110) * 0.12f, wy + 1, C3(0.85f, 1, 1));
    }
    float ix = 90 + trnd() * 30;
    shape(1); ellipse(ix, 150, 70, 14, C3(1.0f, 0.9f, 0.5f));
    for (int p = 0; p < 3; p++) {
        float tx = ix - 30 + p * 28, lean = (p - 1) * 8.0f + 4, h = 70 + p * 6 - (p == 1) * 14;
        float top = 146 - h;
        shape(1); quad(tx - 3, 146, tx + 3, 146, tx + lean + 2, top, tx + lean - 2, top, C3(0.6f, 0.35f, 0.1f));
        for (int k = 0; k < 7; k++) {
            float a = -3.0f + k * 0.5f;
            float ex = tx + lean + cosf(a) * 30, ey = top + sinf(a) * 14 + 12;
            shape(1); quad(tx + lean, top, ex, ey, ex - cosf(a) * 8, ey - 3, tx + lean, top - 3, C3(0.0f, 0.7f, 0.0f));
        }
        shape(1); disc(tx + lean, top + 3, 3, C3(0.45f, 0.25f, 0.05f), 1);
    }
    float bx = 200 + trnd() * 30;                             /* a sailboat */
    shape(1); quad(bx - 18, 128, bx + 18, 128, bx + 12, 136, bx - 12, 136, C3(0.85f, 0.1f, 0.1f));
    shape(1); tri(bx, 126, bx, 90, bx + 18, 126, C3(1, 1, 1));
    shape(1); tri(bx - 2, 126, bx - 2, 96, bx - 16, 126, C3(1, 1, 0.6f));
}
static void pic_astronaut(void)
{
    vgrad(0, 192, C3(0.0f, 0.0f, 0.1f), C3(0.1f, 0.0f, 0.25f));
    stars(110, 192);
    float pxp = 30 + trnd() * 40;
    shape(1); disc(pxp, 150, 60, C3(0.2f, 0.4f, 0.9f), 1);       /* a planet behind */
    shape(0); for (int i = 0; i < 5; i++) ellipse(pxp - 30 + trnd() * 60, 110 + trnd() * 60, 18, 6, C3(0.2f, 0.75f, 0.3f));
    shape(1); ellipse(128, 200, 95, 55, C3(0.92f, 0.92f, 0.92f));  /* shoulders */
    shape(1); rect(108, 150, 148, 165, C3(0.6f, 0.6f, 0.65f));     /* neck ring */
    shape(1); disc(128, 95, 62, C3(0.95f, 0.95f, 0.95f), 1);        /* helmet */
    shape(1); ellipse(128, 98, 46, 40, C3(0.95f, 0.75f, 0.1f));    /* gold visor */
    shape(0);
    for (int y = 58; y < 140; y++)                                /* a curve of planet in the visor */
        for (int x = 82; x < 175; x++) {
            float u = (x - 128) / 46.0f, v = (y - 98) / 40.0f;
            if (u * u + v * v < 1) {
                float d = sqrtf((u + 0.4f) * (u + 0.4f) + (v - 0.9f) * (v - 0.9f));
                if (d < 0.7f) px(x, y, C3(0.2f, 0.35f, 0.85f), 1);
                else if (d < 0.78f) px(x, y, C3(0.4f, 0.9f, 1.0f), 1);
                float dk = u * 0.7f + v * 0.7f;
                if (dk > 0.55f) px(x, y, C3(0.55f, 0.3f, 0.05f), 1);
            }
        }
    glow(108, 76, 18, C3(1, 1, 1), 1.0f);                         /* reflection */
    shape(1); rect(84, 170, 104, 186, C3(0.85f, 0.1f, 0.1f));    /* badges */
    shape(1); rect(152, 170, 172, 186, C3(0.1f, 0.3f, 0.9f));
    rect(155, 175, 169, 177, C3(1, 1, 1));
    shape(1); rect(60, 104, 68, 128, C3(0.6f, 0.6f, 0.65f));    /* helmet light */
    rect(188, 104, 196, 128, C3(0.6f, 0.6f, 0.65f));
    shape(0); disc(64, 108, 3, C3(1, 0.2f, 0.2f), 1); disc(192, 108, 3, C3(0.2f, 1, 0.2f), 1);
}
static void pic_explorer(void)
{
    vgrad(0, 192, C3(0.15f, 0.55f, 0.15f), C3(0.0f, 0.25f, 0.05f));
    for (int i = 0; i < 40; i++) {                                 /* jungle leaves */
        float lx = trnd() * TW, ly = trnd() * 192, a = trnd() * 6.28f, l = 14 + trnd() * 18;
        shape(1); quad(lx, ly, lx + cosf(a) * l * 0.5f + sinf(a) * 5, ly + sinf(a) * l * 0.5f - cosf(a) * 5,
                       lx + cosf(a) * l, ly + sinf(a) * l,
                       lx + cosf(a) * l * 0.5f - sinf(a) * 5, ly + sinf(a) * l * 0.5f + cosf(a) * 5,
                       trnd() < 0.5f ? C3(0.1f, 0.75f, 0.1f) : C3(0.0f, 0.45f, 0.05f));
    }
    float skin[3] = { 1.0f, 0.72f, 0.42f }, shade[3] = { 0.85f, 0.4f, 0.18f };
    shape(1); ellipse(128, 205, 90, 60, C3(0.8f, 0.7f, 0.45f));    /* shirt */
    shape(1); tri(110, 150, 146, 150, 128, 180, skin);              /* open collar */
    shape(1); tri(100, 152, 156, 152, 128, 172, C3(0.9f, 0.1f, 0.1f)); /* neckerchief */
    shape(1); rect(112, 128, 144, 152, shade);                      /* neck */
    shape(1); ellipse(128, 100, 34, 42, skin);                      /* face */
    shape(0); ellipse(144, 104, 16, 36, shade);
    ellipse(128, 100, 30, 38, skin);
    shape(1); ellipse(94, 102, 6, 10, skin); ellipse(162, 102, 6, 10, skin); /* ears */
    shape(1); ellipse(114, 96, 7, 4, C3(1, 1, 1)); shape(0); disc(115, 96, 3, C3(0.2f, 0.4f, 0.9f), 1); disc(115, 96, 1.2f, C3(0, 0, 0), 1);
    shape(1); ellipse(142, 96, 7, 4, C3(1, 1, 1)); shape(0); disc(143, 96, 3, C3(0.2f, 0.4f, 0.9f), 1); disc(143, 96, 1.2f, C3(0, 0, 0), 1);
    shape(0); line(105, 87, 122, 86, 2.5f, C3(0.35f, 0.2f, 0.05f)); line(134, 86, 151, 87, 2.5f, C3(0.35f, 0.2f, 0.05f));
    tri(128, 96, 122, 114, 132, 114, shade);                         /* nose */
    shape(0); line(116, 124, 140, 124, 2, C3(0.6f, 0.15f, 0.1f));    /* a grin */
    line(116, 124, 112, 121, 1.5f, C3(0.6f, 0.15f, 0.1f));
    for (int i = 0; i < 60; i++) px(108 + (int)(trnd() * 40), 118 + (int)(trnd() * 22), shade, 1); /* stubble */
    shape(1); ellipse(128, 70, 66, 12, C3(0.6f, 0.4f, 0.15f));     /* hat brim */
    shape(1); quad(98, 70, 158, 70, 150, 30, 106, 30, C3(0.7f, 0.5f, 0.2f));
    shape(0); rect(100, 60, 156, 68, C3(0.3f, 0.15f, 0.05f));        /* hat band */
    shape(1); line(176, 192, 240, 40, 4, C3(0.5f, 0.3f, 0.1f));      /* a rope coil over the shoulder */
}
static void pic_wizard(void)
{
    vgrad(0, 192, C3(0.05f, 0.0f, 0.3f), C3(0.3f, 0.0f, 0.4f));
    stars(60, 120);
    shape(1); disc(40 + trnd() * 30, 30, 16, C3(1, 1, 0.85f), 1);
    shape(1); quad(170, 192, 230, 192, 222, 40, 178, 40, C3(0.4f, 0.4f, 0.45f));  /* tower */
    for (int i = 0; i < 4; i++) rect(176 + i * 12, 32, 182 + i * 12, 40, C3(0.4f, 0.4f, 0.45f));
    shape(1); rect(195, 70, 205, 86, C3(1, 0.9f, 0.2f));
    shape(1); ridge(178, 4, 0.03f, 0.07f, trnd() * 6, 0, C3(0.1f, 0.3f, 0.1f));
    float cx = 100;
    shape(1); quad(cx - 22, 80, cx + 22, 80, cx + 48, 186, cx - 48, 186, C3(0.1f, 0.2f, 0.85f));  /* robe */
    shape(0); for (int i = 0; i < 12; i++) {
        float sx = cx - 30 + trnd() * 60, sy = 100 + trnd() * 80;
        line(sx - 3, sy, sx + 3, sy, 1, C3(1, 1, 0.3f)); line(sx, sy - 3, sx, sy + 3, 1, C3(1, 1, 0.3f));
    }
    shape(1); ellipse(cx, 66, 15, 17, C3(0.95f, 0.7f, 0.5f));                    /* face */
    shape(1); tri(cx - 16, 70, cx + 16, 70, cx, 132, C3(1, 1, 1));               /* beard */
    shape(0); disc(cx - 6, 63, 2, C3(0, 0, 0), 1); disc(cx + 6, 63, 2, C3(0, 0, 0), 1);
    line(cx - 10, 58, cx - 2, 59, 2, C3(1, 1, 1)); line(cx + 2, 59, cx + 10, 58, 2, C3(1, 1, 1));
    shape(1); ellipse(cx, 52, 30, 6, C3(0.75f, 0.0f, 0.75f));                   /* hat */
    shape(1); quad(cx - 18, 50, cx + 18, 50, cx + 10, 18, cx + 4, 6, C3(0.75f, 0.0f, 0.75f));
    tri(cx + 4, 6, cx + 10, 18, cx + 28, 4, C3(0.75f, 0.0f, 0.75f));
    shape(0); for (int i = 0; i < 5; i++) disc(cx - 10 + trnd() * 20, 22 + trnd() * 24, 1.5f, C3(1, 1, 0.3f), 1);
    shape(1); line(cx + 40, 186, cx + 52, 60, 4, C3(0.55f, 0.3f, 0.1f));      /* staff */
    shape(1); ellipse(cx + 28, 120, 8, 6, C3(0.95f, 0.7f, 0.5f));               /* hand */
    shape(0); glow(cx + 52, 56, 30, C3(0.4f, 1, 1), 1.0f);
    shape(1); disc(cx + 52, 56, 7, C3(0.7f, 1, 1), 1);
    for (int i = 0; i < 6; i++) {                                                /* sparks */
        float a = trnd() * 6.28f, r = 14 + trnd() * 20;
        shape(0); px((int)(cx + 52 + cosf(a) * r), (int)(56 + sinf(a) * r), C3(1, 1, 1), 1);
    }
}
static void pic_ship(void)
{
    vgrad(0, 120, C3(0.9f, 0.3f, 0.1f), C3(1.0f, 0.85f, 0.3f));
    float sx = 60 + trnd() * 130;
    shape(0); disc(sx, 112, 26, C3(1, 1, 0.6f), 1);
    for (int i = 0; i < 3; i++) {
        float cx = trnd() * TW, cy = 20 + trnd() * 40;
        ellipse(cx, cy, 30, 5, C3(0.75f, 0.2f, 0.4f));
    }
    vgrad(118, 192, C3(0.1f, 0.25f, 0.6f), C3(0.0f, 0.05f, 0.25f));
    for (int i = 0; i < 30; i++) rect(sx - 20 + trnd() * 40 - i, 120 + i * 2.2f, sx + 20 - trnd() * 30 + i, 121 + i * 2.2f, C3(1, 0.85f, 0.3f));
    float bx = 120 + trnd() * 40;
    shape(1); float hull[] = { bx - 70, 130, bx + 74, 126, bx + 54, 158, bx - 50, 158 };
    poly(4, hull, C3(0.5f, 0.25f, 0.05f));
    shape(0); rect(bx - 60, 140, bx + 62, 143, C3(1, 0.9f, 0.3f));
    for (int i = 0; i < 7; i++) rect(bx - 48 + i * 15, 146, bx - 43 + i * 15, 150, C3(0, 0, 0));
    for (int m = 0; m < 3; m++) {
        float mx = bx - 40 + m * 42, top = 34 + (m == 1) * -12;
        shape(1); line(mx, 130, mx, top, 3, C3(0.35f, 0.15f, 0.0f));
        for (int s = 0; s < 3; s++) {
            float y0 = top + 8 + s * 28, w = 22 - s * 2 + (m == 1) * 4;
            shape(1); quad(mx - w, y0, mx + w, y0, mx + w + 4, y0 + 22, mx - w + 2, y0 + 24, C3(0.95f, 0.95f, 0.85f));
        }
        shape(1); tri(mx, top, mx + 16, top + 4, mx, top + 8, C3(0.0f, 0.0f, 0.0f));
    }
    shape(0); for (int i = 0; i < 80; i++) {
        float wx = trnd() * TW, wy = 158 + trnd() * 34;
        rect(wx, wy, wx + 6, wy + 1, C3(0.6f, 0.85f, 1));
    }
}
static void pic_racer(void)
{
    /* a racing driver in a helmet, a chequered flag behind */
    for (int y = 0; y < TH; y++)
        for (int x = 0; x < TW; x++) {
            float w = sinf(x * 0.03f + y * 0.01f) * 8;
            int c = (((int)((x + w) / 20)) + ((int)((y + w) / 20))) & 1;
            float v = c ? 0.95f : 0.05f;
            px(x, y, C3(v, v, v), 1);
        }
    shape(1); ellipse(128, 210, 110, 70, C3(0.9f, 0.1f, 0.1f));            /* overalls */
    shape(0); rect(60, 160, 196, 170, C3(1, 1, 1));
    shape(1); disc(128, 100, 66, C3(0.1f, 0.3f, 0.95f), 1);                 /* helmet */
    shape(0); for (int y = 34; y < 166; y++) for (int x = 62; x < 194; x++) {
        float d = (x - 128) * 0.4f + (y - 100) * 0.92f;
        if (fabsf(d - 10) < 6 && (x - 128) * (x - 128) + (y - 100) * (y - 100) < 66 * 66) px(x, y, C3(1, 1, 0.2f), 1);
    }
    shape(1); quad(76, 84, 180, 84, 172, 118, 84, 118, C3(0.05f, 0.05f, 0.05f)); /* visor */
    shape(0); ellipse(128, 101, 46, 13, C3(0.95f, 0.65f, 0.4f));            /* the eyes in the slot */
    shape(1); ellipse(112, 100, 7, 4, C3(1, 1, 1)); shape(0); disc(113, 100, 2.5f, C3(0.3f, 0.6f, 0.2f), 1);
    shape(1); ellipse(144, 100, 7, 4, C3(1, 1, 1)); shape(0); disc(145, 100, 2.5f, C3(0.3f, 0.6f, 0.2f), 1);
    shape(0); line(102, 92, 122, 93, 2.5f, C3(0.3f, 0.15f, 0.05f)); line(134, 93, 154, 92, 2.5f, C3(0.3f, 0.15f, 0.05f));
    glow(100, 60, 22, C3(1, 1, 1), 0.8f);
    shape(1); rect(108, 140, 148, 150, C3(1, 1, 1));
}

typedef void (*PicFn)(void);
static const struct { const char *name; PicFn fn; } PICS[] = {
    { "LIGHTHOUSE", pic_lighthouse }, { "CASTLE", pic_castle }, { "NIGHT CITY", pic_city },
    { "PYRAMIDS", pic_desert }, { "MOUNTAINS", pic_mountains }, { "ISLAND", pic_island },
    { "SPACEMAN", pic_astronaut }, { "EXPLORER", pic_explorer }, { "WIZARD", pic_wizard },
    { "GALLEON", pic_ship }, { "RACER", pic_racer },
};
#define NPICS ((int)(sizeof PICS / sizeof PICS[0]))

/* ---- the screen memory ---- */
static const float ZXC[16][3] = {
    {0,0,0},{0,0,.84f},{.84f,0,0},{.84f,0,.84f},{0,.84f,0},{0,.84f,.84f},{.84f,.84f,0},{.84f,.84f,.84f},
    {0,0,0},{0,0,1},{1,0,0},{1,0,1},{0,1,0},{0,1,1},{1,1,0},{1,1,1},
};
typedef struct { unsigned char bits[TH][32]; unsigned char attr[24][32]; } ZXScreen;

static const float BAYER[4][4] = {
    { 0.5f/16, 8.5f/16, 2.5f/16, 10.5f/16 }, { 12.5f/16, 4.5f/16, 14.5f/16, 6.5f/16 },
    { 3.5f/16, 11.5f/16, 1.5f/16, 9.5f/16 }, { 15.5f/16, 7.5f/16, 13.5f/16, 5.5f/16 },
};

/* the canvas -> one bit per pixel plus an ink/paper pair per cell */
static void zx_convert(ZXScreen *s)
{
    for (int cy = 0; cy < 24; cy++)
        for (int cx = 0; cx < 32; cx++) {
            float best = 1e30f; int bi = 0, bj = 7;
            for (int br = 0; br < 2; br++)
                for (int i = 0; i < 8; i++)
                    for (int j = i; j < 8; j++) {
                        const float *a = ZXC[br * 8 + i], *b = ZXC[br * 8 + j];
                        float ab[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
                        float ll = ab[0] * ab[0] + ab[1] * ab[1] + ab[2] * ab[2];
                        float e = 0;
                        for (int y = 0; y < 8 && e < best; y++)
                            for (int x = 0; x < 8; x++) {
                                const float *p = g_cv[cy * 8 + y][cx * 8 + x];
                                float t = ll > 0 ? ((p[0] - a[0]) * ab[0] + (p[1] - a[1]) * ab[1] + (p[2] - a[2]) * ab[2]) / ll : 0;
                                t = t < 0 ? 0 : t > 1 ? 1 : t;
                                float d0 = p[0] - a[0] - ab[0] * t, d1 = p[1] - a[1] - ab[1] * t, d2 = p[2] - a[2] - ab[2] * t;
                                /* how far the mixed colour is off, plus how far each dot is
                                 * from what it stands for: so a sunset is red and yellow
                                 * dots, not red and cyan ones that happen to average out */
                                float ea = (p[0] - a[0]) * (p[0] - a[0]) + (p[1] - a[1]) * (p[1] - a[1]) + (p[2] - a[2]) * (p[2] - a[2]);
                                float eb = (p[0] - b[0]) * (p[0] - b[0]) + (p[1] - b[1]) * (p[1] - b[1]) + (p[2] - b[2]) * (p[2] - b[2]);
                                e += d0 * d0 + d1 * d1 + d2 * d2 + 0.8f * ((1.0f - t) * ea + t * eb);
                            }
                        if (e < best) { best = e; bi = br * 8 + i; bj = br * 8 + j; }
                    }
            /* ink = the darker of the two, so while the pixels load in black on
             * white, before the colours arrive, the picture already reads right */
            const float *a = ZXC[bi], *b = ZXC[bj];
            float ab[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
            float ll = ab[0] * ab[0] + ab[1] * ab[1] + ab[2] * ab[2];
            for (int y = 0; y < 8; y++) {
                unsigned char byte = 0;
                for (int x = 0; x < 8; x++) {
                    const float *p = g_cv[cy * 8 + y][cx * 8 + x];
                    float t = ll > 0 ? ((p[0] - a[0]) * ab[0] + (p[1] - a[1]) * ab[1] + (p[2] - a[2]) * ab[2]) / ll : 0;
                    if (t <= BAYER[y & 3][x & 3]) byte |= (unsigned char)(0x80 >> x);
                }
                s->bits[cy * 8 + y][cx] = byte;
            }
            int bright = bj >= 8 || bi >= 8;
            s->attr[cy][cx] = (unsigned char)((bright << 6) | ((bj & 7) << 3) | (bi & 7));   /* paper b, ink a */
        }
}

/* screen memory -> RGB, one row of pixels */
static void zx_row(const ZXScreen *s, int y, unsigned char *out)
{
    for (int x = 0; x < TW; x++) {
        unsigned char at = s->attr[y / 8][x / 8];
        int on = (s->bits[y][x / 8] >> (7 - (x & 7))) & 1;
        int c = (on ? (at & 7) : ((at >> 3) & 7)) + ((at >> 6) & 1) * 8;
        for (int k = 0; k < 3; k++) out[x * 3 + k] = (unsigned char)(ZXC[c][k] * 255.0f + 0.5f);
    }
}


/* ---- your own photos: any .jpg, .png, .bmp or .gif in a "tapes" folder next
 * to the program. Smaller files load faster; 1024 pixels across is plenty. ---- */
#define MAXPHOTO 200
static char *g_photo[MAXPHOTO];
static int   g_nphoto;

static int is_image(const char *n)
{
    const char *e = strrchr(n, '.');
    if (!e) return 0;
    char x[8] = { 0 };
    for (int i = 0; i < 6 && e[i + 1]; i++) x[i] = (char)(e[i + 1] | 0x20);
    return !strcmp(x, "jpg") || !strcmp(x, "jpeg") || !strcmp(x, "png") ||
           !strcmp(x, "bmp") || !strcmp(x, "gif");
}

static void photos_scan(void)
{
    DIR *d = opendir(dome_path("tapes"));
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) && g_nphoto < MAXPHOTO)
        if (e->d_name[0] != '.' && is_image(e->d_name)) {
            size_t n = strlen(e->d_name) + 1;
            g_photo[g_nphoto] = malloc(n);
            memcpy(g_photo[g_nphoto++], e->d_name, n);
        }
    closedir(d);
    if (g_nphoto) printf("Retro: %d photo%s in the tapes folder\n", g_nphoto, g_nphoto == 1 ? "" : "s");
}

/* the photo, cropped to 4:3 and averaged down onto the canvas, with the
 * colour and contrast pushed up the way the old artists did */
static int photo_load(const char *file)
{
    char rel[300];
    snprintf(rel, sizeof rel, "tapes/%s", file);
    int w, h, n;
    unsigned char *img = stbi_load(dome_path(rel), &w, &h, &n, 3);
    if (!img) { printf("Retro: could not read %s\n", rel); return 0; }
    float cw = (float)w, ch = cw * 0.75f;
    if (ch > h) { ch = (float)h; cw = ch / 0.75f; }
    float x0 = (w - cw) * 0.5f, y0 = (h - ch) * 0.5f;
    for (int y = 0; y < TH; y++)
        for (int x = 0; x < TW; x++) {
            int ax = (int)(x0 + x * cw / TW), bx = (int)(x0 + (x + 1) * cw / TW);
            int ay = (int)(y0 + y * ch / TH), by = (int)(y0 + (y + 1) * ch / TH);
            if (bx <= ax) bx = ax + 1;
            if (by <= ay) by = ay + 1;
            int sx = bx - ax > 4 ? (bx - ax) / 4 : 1, sy = by - ay > 4 ? (by - ay) / 4 : 1;
            float s[3] = { 0, 0, 0 }; int k = 0;
            for (int yy = ay; yy < by && yy < h; yy += sy)
                for (int xx = ax; xx < bx && xx < w; xx += sx, k++)
                    for (int i = 0; i < 3; i++) s[i] += img[((size_t)yy * w + xx) * 3 + i];
            float c[3], l;
            for (int i = 0; i < 3; i++) c[i] = k ? s[i] / (255.0f * k) : 0;
            l = 0.3f * c[0] + 0.59f * c[1] + 0.11f * c[2];
            for (int i = 0; i < 3; i++) {
                float v = l + (c[i] - l) * 1.35f;              /* more colour */
                v = (v - 0.5f) * 1.15f + 0.5f;                 /* more contrast */
                g_cv[y][x][i] = v < 0 ? 0 : v > 1 ? 1 : v;
            }
        }
    stbi_image_free(img);
    return 1;
}

/* ---- the load itself ---- */
enum { TP_CLS, TP_PILOT1, TP_HEADER, TP_GAP, TP_PILOT2, TP_DATA, TP_DONE };
static const float TP_LEN[] = { 1.0f, 2.2f, 0.35f, 0.8f, 1.6f, 16.0f, 9.0f };
#define TP_BYTES 6912                   /* 6144 of pixels, then 768 of colour */

static ZXScreen g_scr, g_pic;           /* what the TV shows; what is on the tape */
static GLuint g_scr_tex;
static unsigned char g_scr_rgb[TH][TW * 3];
static int   g_dirty0 = TH, g_dirty1 = -1;
static int   g_tp_phase, g_tp_bytes, g_tp_border;
static float g_tp_t, g_tp_stripe;
static char  g_tp_name[11];
static int   g_deck[NPICS + MAXPHOTO], g_ndeck, g_last = -1;

static void scr_dirty(int y0, int y1)
{
    if (y0 < g_dirty0) g_dirty0 = y0;
    if (y1 > g_dirty1) g_dirty1 = y1;
}

static void scr_print(int col, int row, const char *s)
{
    for (; *s && col < 32; s++, col++) {
        int ch = (unsigned char)*s;
        const unsigned char *g = FONT5X7[(ch >= 32 && ch < 127 ? ch : '?') - 32];
        for (int y = 0; y < 8; y++)
            g_scr.bits[row * 8 + y][col] = y < 7 ? (unsigned char)(g[y] << 2) : 0;
    }
    scr_dirty(row * 8, row * 8 + 7);
}

/* pick the next picture: a shuffled deck of everything, no repeats until
 * it has all been shown, and never the same one twice running */
static void tp_pick(void)
{
    int total = NPICS + g_nphoto;
    if (g_ndeck == 0) {
        for (int i = 0; i < total; i++) g_deck[i] = i;
        for (int i = total - 1; i > 0; i--) { int j = rand() % (i + 1), t = g_deck[i]; g_deck[i] = g_deck[j]; g_deck[j] = t; }
        g_ndeck = total;
        if (total > 1 && g_deck[g_ndeck - 1] == g_last) { int t = g_deck[0]; g_deck[0] = g_deck[g_ndeck - 1]; g_deck[g_ndeck - 1] = t; }
    }
    int pick = g_deck[--g_ndeck];
    g_last = pick;
    memset(g_cv, 0, sizeof g_cv);
    memset(g_cid, 0, sizeof g_cid);
    g_id = 0;
    const char *name = NULL;
    char buf[300];
    if (pick >= NPICS && photo_load(g_photo[pick - NPICS])) {
        snprintf(buf, sizeof buf, "%s", g_photo[pick - NPICS]);
        char *dot = strrchr(buf, '.'); if (dot) *dot = 0;
        for (char *c = buf; *c; c++) if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 32);
        name = buf;
    } else {
        if (pick >= NPICS) pick = rand() % NPICS;
        g_seed = (unsigned)rand() * 2654435761u + 1u;
        PICS[pick].fn();
        outline_pass();
        name = PICS[pick].name;
    }
    size_t nl = strlen(name) < 10 ? strlen(name) : 10;      /* tape names are ten letters */
    memcpy(g_tp_name, name, nl); g_tp_name[nl] = 0;
    zx_convert(&g_pic);
}

static void tp_phase(int ph)
{
    g_tp_phase = ph; g_tp_t = 0;
    if (ph == TP_CLS) {                                     /* a fresh LOAD "": white screen */
        memset(g_scr.bits, 0, sizeof g_scr.bits);
        memset(g_scr.attr, 0x38, sizeof g_scr.attr);        /* white paper, black ink */
        scr_dirty(0, TH - 1);
        g_tp_border = 7;
        tp_pick();
        printf("Retro: loading %s\n", g_tp_name);
    }
    if (ph == TP_GAP) {
        char line[40];
        snprintf(line, sizeof line, "Program: %s", g_tp_name);
        scr_print(0, 0, line);
    }
    if (ph == TP_DATA) g_tp_bytes = 0;
    if (ph == TP_DONE) g_tp_border = 0;
}

static void tp_reset(void) { g_ndeck = 0; tp_phase(TP_CLS); }

static void tp_update(float dt)
{
    g_tp_t += dt;
    if (g_tp_phase == TP_PILOT1 || g_tp_phase == TP_PILOT2) g_tp_stripe += dt * 9.0f;
    if (g_tp_phase == TP_HEADER || g_tp_phase == TP_DATA) g_tp_stripe += 1.0f;
    if (g_tp_phase == TP_DATA) {
        int want = (int)(g_tp_t / TP_LEN[TP_DATA] * TP_BYTES);
        if (want > TP_BYTES) want = TP_BYTES;
        for (; g_tp_bytes < want; g_tp_bytes++) {
            int i = g_tp_bytes;
            if (i < 6144) {                                 /* the pixels, in screen-memory order */
                int l = i / 32, col = i % 32;
                int y = (l / 64) * 64 + (l % 8) * 8 + (l / 8) % 8;
                g_scr.bits[y][col] = g_pic.bits[y][col];
                scr_dirty(y, y);
            } else {                                        /* then the colours, row by row */
                int j = i - 6144, row = j / 32;
                g_scr.attr[row][j % 32] = g_pic.attr[row][j % 32];
                scr_dirty(row * 8, row * 8 + 7);
            }
        }
    }
    if (g_tp_t >= TP_LEN[g_tp_phase]) tp_phase(g_tp_phase == TP_DONE ? TP_CLS : g_tp_phase + 1);
}

static void tp_upload(void)                 /* uses unit 1: unit 0 holds the lens lookup */
{
    glActiveTexture(GL_TEXTURE1);
    if (!g_scr_tex) {
        glGenTextures(1, &g_scr_tex);
        glBindTexture(GL_TEXTURE_2D, g_scr_tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, TW, TH, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
        g_dirty0 = 0; g_dirty1 = TH - 1;
    }
    if (g_dirty1 < g_dirty0) return;
    for (int y = g_dirty0; y <= g_dirty1; y++) zx_row(&g_scr, y, g_scr_rgb[y]);
    glBindTexture(GL_TEXTURE_2D, g_scr_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, g_dirty0, TW, g_dirty1 - g_dirty0 + 1,
                    GL_RGB, GL_UNSIGNED_BYTE, g_scr_rgb[g_dirty0]);
    g_dirty0 = TH; g_dirty1 = -1;
}

static void tp_uniforms(GLuint p)
{
    tp_upload();
    glBindTexture(GL_TEXTURE_2D, g_scr_tex);
    glUniform1i(glGetUniformLocation(p, "u_scr"), 1);
    glActiveTexture(GL_TEXTURE0);
    float mode = g_tp_phase == TP_PILOT1 || g_tp_phase == TP_PILOT2 ? 1.0f
               : g_tp_phase == TP_HEADER || (g_tp_phase == TP_DATA && g_tp_bytes < TP_BYTES) ? 2.0f : 0.0f;
    glUniform4f(glGetUniformLocation(p, "u_tb"), mode, fmodf(g_tp_stripe, 1000.0f), 0, 0);
    const float *bc = ZXC[g_tp_border];
    glUniform3f(glGetUniformLocation(p, "u_bc"), bc[0], bc[1], bc[2]);
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
    photos_scan();
    tp_reset();
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
    if (s == 8) tp_update(dt);
    if (s == 3) lander_step(dt);
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
    if (s == 8) tp_uniforms(p);
    if (s == 3) lander_uniforms(p);
    if (s == 6) glUniform4f(glGetUniformLocation(p, "u_g"), 0, A.flash > 0 ? A.flash : 0.0f, 0, 0);
    dome_surface_end();
    mat3 still; m3_identity(still);
    if (s == 5) {
        defender_draw(g_t);
        dome_draw_points(&g_pix, still, f->fade, 5.0f);
    }
    if (s == 3) {
        lander_draw();
        dome_draw_points(&g_pix, still, f->fade, 3.5f);
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
