/*
 * seasons.c  -  seasons and celebrations. N steps through the styles:
 *
 *   0 fireworks        Bonfire Night: bursts over a glowing bonfire
 *   1 bauble           the globe as a glossy Christmas bauble, snow falling
 *   2 snowfall         snow on a winter night, pines on the skyline
 *   3 fairy lights     strings of coloured bulbs round the globe, twinkling
 *   4 pumpkin          a carved pumpkin face with a flickering candle
 *   5 spooky night     full moon, bats and dark hills
 *   6 Easter egg       the globe as a painted egg
 *   7 spring blossom   pink and white petals drifting down
 *   8 New Year         the countdown to 1 January, with gold confetti
 *   9 autumn leaves    red, orange and gold leaves falling
 *
 * Set auto to 1 and it picks the style from today's date: fireworks in
 * early November, pumpkin and spooky night for Halloween week, Christmas
 * styles through December, the New Year countdown on 31 December and
 * 1 January, snow in January and February, blossom and Easter in spring,
 * leaves in autumn. Pressing N turns auto off.
 *
 * Fireworks and the New Year text are dots drawn over a shader
 * background; everything else is one small shader per style.
 */
#include "../dome/dome.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define NSTYLE 10
static const char *NAMES[NSTYLE] = {
    "fireworks", "bauble", "snowfall", "fairy lights", "pumpkin",
    "spooky night", "Easter egg", "spring blossom", "New Year", "autumn leaves"
};

static struct { float style, autopick, speed, scale, bright; } L = { 0.0f, 0.0f, 1.0f, 1.0f, 1.0f };

static Param P[] = {
    { "style",  &L.style,    1.0f, 0.0f, 9.0f, 0,
      "0 fireworks, 1 bauble, 2 snowfall, 3 fairy lights, 4 pumpkin, 5 spooky night, "
      "6 Easter egg, 7 blossom, 8 New Year, 9 autumn leaves (N)" },
    { "auto",   &L.autopick, 1.0f, 0.0f, 1.0f, 0, "1 = pick the style from today's date" },
    { "speed",  &L.speed,    0.1f, 0.0f, 4.0f, 0, "speed of the animation" },
    { "scale",  &L.scale,    0.05f, 0.4f, 2.5f, 0, "size of the pattern (bigger = more, smaller)" },
    { "bright", &L.bright,   0.05f, 0.2f, 2.0f, 0, "brightness" },
};

static const char LIB[] =
    "uniform float u_t, u_scale, u_bright;\n"
    "vec2 plane(vec3 v){ return v.xy / (1.0 + max(v.z, 0.0)); }\n"
    "vec2 rot(vec2 p, float a){ float c = cos(a), s = sin(a); return vec2(c * p.x - s * p.y, s * p.x + c * p.y); }\n"
    "vec3 lit(vec3 c, vec3 view, float gloss, float spec){\n"
    "  vec3 Lt = normalize(vec3(-0.45, 0.55, 0.70));\n"
    "  vec3 R = reflect(-Lt, view);\n"
    "  return c * (0.30 + 0.70 * max(dot(view, Lt), 0.0)) + vec3(pow(max(R.z, 0.0), gloss) * spec);\n"
    "}\n"
    /* falling snow in layers: 0 to 1 */
    "float snow(vec2 p, float t){\n"
    "  float s = 0.0;\n"
    "  for (int k = 0; k < 3; k++) {\n"
    "    float fk = float(k), sc = (6.0 + fk * 5.0) * u_scale;\n"
    "    vec2 q = p * sc + vec2(sin(t * 0.3 + fk) * 0.5, t * (0.8 + fk * 0.3));\n"
    "    vec2 c = floor(q), f = fract(q) - 0.5;\n"
    "    float h = h3(vec3(c, fk));\n"
    "    vec2 o = (vec2(h, fract(h * 9.1)) - 0.5) * 0.6;\n"
    "    s = max(s, step(0.55, h) * smoothstep(0.13 - fk * 0.02, 0.02, length(f - o)) * (1.0 - fk * 0.2));\n"
    "  }\n"
    "  return s;\n"
    "}\n";

/* ---- background for fireworks: night sky and a bonfire glow ---- */
static const char NIGHT[] =
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  vec2 p = plane(view);\n"
    "  vec3 c = mix(vec3(0.01, 0.01, 0.05), vec3(0.03, 0.04, 0.12), 0.5 + 0.5 * p.y);\n"
    "  vec2 g = floor(p * 90.0);\n"
    "  c += vec3(0.6) * step(0.996, h3(vec3(g, 1.0))) * (0.6 + 0.4 * sin(u_t * 3.0 + h3(vec3(g, 2.0)) * 20.0));\n"
    "  float fl = 0.8 + 0.2 * sin(u_t * 9.0) * sin(u_t * 5.3);\n"
    "  float glow = exp(-length((p - vec2(0.0, -0.85)) * vec2(1.2, 2.2)) * 3.0) * fl;\n"
    "  c += vec3(1.0, 0.45, 0.10) * glow;\n"
    "  float fire = -0.62 + 0.10 * sin(p.x * 25.0 + u_t * 6.0) * sin(p.x * 11.0 - u_t * 4.0) - abs(p.x) * 0.6;\n"
    "  if (p.y < fire && abs(p.x) < 0.3) c = mix(vec3(1.0, 0.35, 0.05), vec3(1.0, 0.85, 0.3), smoothstep(fire - 0.15, fire, p.y) * 0.5 + 0.3);\n"
    "  if (p.y < -0.75 + 0.03 * sin(p.x * 7.0)) c = vec3(0.02, 0.03, 0.02);\n"   /* the ground */
    "  return vec4(c * u_bright, 1.0);\n"
    "}\n";

/* ---- 1: Christmas bauble ---- */
static const char BAUBLE[] =
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  float lat = asin(clamp(dir.y, -1.0, 1.0)), lon = atan(dir.x, dir.z);\n"
    "  vec3 c = vec3(0.70, 0.04, 0.06);\n"
    "  float b = min(abs(lat - 0.35), abs(lat + 0.35));\n"
    "  if (b < 0.045) c = vec3(0.95, 0.75, 0.25);\n"                          /* gold bands */
    "  if (abs(lat) < 0.30) {\n"
    "    vec2 q = vec2(lon * 6.0 / 3.14159, lat * 6.0);\n"                     /* diamonds */
    "    vec2 f = abs(fract(q) - 0.5);\n"
    "    if (f.x + f.y < 0.18) c = vec3(0.97);\n"
    "  }\n"
    "  if (abs(abs(lat) - 0.6) < 0.06) {\n"                                  /* little dots */
    "    float fx = fract(lon * 18.0 / 6.2832) - 0.5;\n"
    "    if (length(vec2(fx * 0.35, abs(lat) - 0.6)) < 0.03) c = vec3(0.97, 0.85, 0.4);\n"
    "  }\n"
    "  vec3 col = lit(c, view, 70.0, 0.9);\n"
    "  vec2 w = plane(reflect(-view, vec3(0.0, 0.0, 1.0)) * vec3(-1.0, 1.0, 1.0));\n"  /* window glint */
    "  col += vec3(0.5) * step(abs(view.x + 0.30), 0.05) * step(abs(view.y - 0.38), 0.07);\n"
    "  col = mix(col, vec3(1.0), snow(plane(view), u_t) * 0.9);\n"
    "  return vec4(col * u_bright, 1.0);\n"
    "}\n";

/* ---- 2: snowfall ---- */
static const char SNOWFALL[] =
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  vec2 p = plane(view);\n"
    "  vec3 c = mix(vec3(0.02, 0.04, 0.12), vec3(0.10, 0.15, 0.30), 0.5 - 0.5 * p.y);\n"
    "  vec2 g = floor(p * 80.0);\n"
    "  c += vec3(0.7) * step(0.995, h3(vec3(g, 3.0))) * step(0.0, p.y);\n"
    "  float sky = -0.30;\n"                                                 /* pines on the skyline */
    "  float tx = p.x * 7.0 * u_scale;\n"
    "  float ti = floor(tx), tf = fract(tx) - 0.5;\n"
    "  float th = 0.16 + 0.12 * h3(vec3(ti, 4.0, 4.0));\n"
    "  float ty = (p.y - sky) / th;\n"
    "  float layer = fract(ty * 3.0);\n"
    "  if (ty > 0.0 && ty < 1.0 && abs(tf) < (1.0 - ty) * 0.42 * (0.7 + 0.3 * (1.0 - layer))) c = vec3(0.02, 0.08, 0.05);\n"
    "  if (p.y < sky) c = vec3(0.02, 0.07, 0.04);\n"
    "  float drift = -0.45 + 0.06 * sin(p.x * 5.0) + 0.03 * sin(p.x * 13.0);\n"
    "  if (p.y < drift) c = vec3(0.80, 0.85, 0.95) * (0.85 + 0.15 * (p.y - drift + 0.3));\n"
    "  c = mix(c, vec3(1.0), snow(p, u_t));\n"
    "  return vec4(c * u_bright, 1.0);\n"
    "}\n";

/* ---- 3: fairy lights ---- */
static const char LIGHTS[] =
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  float lat = asin(clamp(dir.y, -1.0, 1.0)), lon = atan(dir.x, dir.z);\n"
    "  vec3 c = vec3(0.01, 0.03, 0.06);\n"
    "  for (int i = 0; i < 5; i++) {\n"
    "    float fi = float(i);\n"
    "    float n = floor((14.0 - abs(fi - 2.0) * 3.0) * u_scale + 0.5);\n"  /* bulbs per string */
    "    float a = (lon / 6.2832 + 0.5) * n + fi * 0.37;\n"
    "    float k = floor(a), f = fract(a);\n"
    "    float wire = 0.95 - fi * 0.45 - 0.07 * sin(f * 3.14159);\n"          /* the wire sags */
    "    float wd = abs(lat - wire) * 30.0;\n"
    "    c = mix(c, vec3(0.08, 0.12, 0.08), smoothstep(0.25, 0.0, wd) * 0.8);\n"
    "    vec2 bp = vec2((f - 0.5) * cos(lat) * 6.2832 / n, lat - (wire - 0.07));\n"
    "    float bd = length(bp * vec2(1.0, 0.75));\n"
    "    float ci = mod(k + fi, 5.0);\n"
    "    vec3 bc = ci < 1.0 ? vec3(1.0, 0.15, 0.1) : ci < 2.0 ? vec3(0.1, 0.9, 0.2) : ci < 3.0 ? vec3(0.2, 0.4, 1.0)\n"
    "            : ci < 4.0 ? vec3(1.0, 0.8, 0.1) : vec3(1.0, 0.4, 0.9);\n"
    "    float on = 0.35 + 0.65 * step(0.0, sin(u_t * (1.0 + h3(vec3(k, fi, 1.0)) * 2.0) + k * 1.7));\n"
    "    c += bc * on * smoothstep(0.05, 0.0, bd) * 1.2;\n"
    "    c += bc * on * exp(-bd * 18.0) * 0.35;\n"                        /* glow */
    "  }\n"
    "  return vec4(c * u_bright, 1.0);\n"
    "}\n";

/* ---- 4: pumpkin (faces you) ---- */
static const char PUMPKIN[] =
    "float tri2(vec2 p, vec2 a, vec2 b, vec2 c){\n"                        /* inside a triangle? */
    "  float s1 = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);\n"
    "  float s2 = (c.x - b.x) * (p.y - b.y) - (c.y - b.y) * (p.x - b.x);\n"
    "  float s3 = (a.x - c.x) * (p.y - c.y) - (a.y - c.y) * (p.x - c.x);\n"
    "  return (s1 >= 0.0 && s2 >= 0.0 && s3 >= 0.0) || (s1 <= 0.0 && s2 <= 0.0 && s3 <= 0.0) ? 1.0 : 0.0;\n"
    "}\n"
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  vec2 p = plane(view) / u_scale;\n"
    "  float rib = cos(atan(view.x, view.z) * 9.0);\n"
    "  vec3 c = mix(vec3(0.55, 0.20, 0.02), vec3(0.95, 0.45, 0.05), 0.6 + 0.4 * rib);\n"
    "  c = lit(c, view, 25.0, 0.2);\n"
    "  if (view.y > 0.93) c = vec3(0.25, 0.30, 0.10);\n"                    /* the stalk */
    "  float hole = 0.0;\n"
    "  vec2 q = vec2(abs(p.x), p.y);\n"                                     /* the face is symmetric */
    "  hole = max(hole, tri2(q, vec2(0.12, 0.10), vec2(0.42, 0.10), vec2(0.26, 0.36)));\n"   /* eyes */
    "  hole = max(hole, tri2(p, vec2(-0.07, -0.08), vec2(0.07, -0.08), vec2(0.0, 0.06)));\n" /* nose */
    "  float mouth = step(-0.42 + 0.10 * q.x * q.x, p.y) * step(p.y, -0.20 - 0.25 * q.x * q.x) * step(q.x, 0.52);\n"
    "  float teeth = step(0.5, fract(p.x * 4.0 + 0.25)) * step(-0.29, p.y) * step(p.y, -0.20 - 0.25 * q.x * q.x);\n"
    "  hole = max(hole, mouth * (1.0 - teeth * step(0.08, q.x)));\n"
    "  float fl = 0.80 + 0.12 * sin(u_t * 11.0) * sin(u_t * 7.3) + 0.08 * sin(u_t * 23.0);\n"
    "  vec3 candle = mix(vec3(1.0, 0.45, 0.05), vec3(1.0, 0.90, 0.40), fl - 0.6) * fl * 1.3;\n"
    "  c = mix(c, candle, hole);\n"
    "  return vec4(c * u_bright, 1.0);\n"
    "}\n";

/* ---- 5: spooky night ---- */
static const char SPOOKY[] =
    "float bat(vec2 p, float flap){\n"
    "  float ax = abs(p.x);\n"
    "  if (ax > 1.0) return 0.0;\n"
    "  float top = 0.10 + flap * 0.35 * ax - 0.10 * ax;\n"
    "  float bot = top - 0.25 + 0.15 * ax + 0.08 * abs(sin(ax * 9.0));\n"     /* scalloped edge */
    "  float body = step(length(p * vec2(2.5, 1.2)), 0.25);\n"
    "  return max(step(bot, p.y) * step(p.y, top), body);\n"
    "}\n"
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  vec2 p = plane(view);\n"
    "  vec3 c = mix(vec3(0.05, 0.02, 0.10), vec3(0.30, 0.12, 0.35), 0.5 - 0.5 * p.y);\n"
    "  vec2 mp = vec2(0.28, 0.32);\n"
    "  float md = length(p - mp);\n"
    "  c += vec3(0.9, 0.85, 0.6) * exp(-md * 5.0) * 0.4;\n"
    "  if (md < 0.17) c = vec3(0.95, 0.92, 0.75) * (0.9 + 0.1 * sin(p.x * 40.0) * sin(p.y * 35.0));\n"
    "  for (int i = 0; i < 6; i++) {\n"
    "    float fi = float(i), h = h3(vec3(fi, 3.0, 9.0));\n"
    "    vec2 bc = vec2(mod(u_t * 0.12 * (1.0 + h) + h * 3.0, 2.6) - 1.3, 0.05 + 0.45 * h + 0.06 * sin(u_t * 0.9 + fi));\n"
    "    float sz = (0.05 + 0.04 * h) / u_scale;\n"
    "    float flap = sin(u_t * (8.0 + h * 4.0) + fi);\n"
    "    if (bat((p - bc) / sz, flap) > 0.5) c = vec3(0.02);\n"
    "  }\n"
    "  float hill = -0.40 + 0.10 * sin(p.x * 3.0 + 1.0) + 0.05 * sin(p.x * 9.0);\n"
    "  if (p.y < hill) c = vec3(0.03, 0.02, 0.05);\n"
    "  return vec4(c * u_bright, 1.0);\n"
    "}\n";

/* ---- 6: Easter egg ---- */
static const char EGG[] =
    "vec3 pastel(float i){\n"
    "  i = mod(i, 5.0);\n"
    "  return i < 1.0 ? vec3(0.98, 0.70, 0.80) : i < 2.0 ? vec3(0.70, 0.85, 1.0) : i < 3.0 ? vec3(0.75, 0.95, 0.75)\n"
    "       : i < 4.0 ? vec3(1.0, 0.92, 0.65) : vec3(0.85, 0.75, 1.0);\n"
    "}\n"
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  float lat = asin(clamp(dir.y, -1.0, 1.0)), lon = atan(dir.x, dir.z);\n"
    "  float bands = 7.0 * u_scale;\n"
    "  float bf = (lat / 3.14159 + 0.5) * bands, bi = floor(bf), f = fract(bf);\n"
    "  vec3 c = pastel(bi);\n"
    "  float kind = mod(bi, 3.0);\n"
    "  float x = lon / 6.2832 * 16.0;\n"
    "  if (kind < 1.0) {\n"                                                 /* zigzag */
    "    if (abs(f - 0.5 - (abs(fract(x) - 0.5) - 0.25) * 0.8) < 0.09) c = pastel(bi + 2.0) * 0.8;\n"
    "  } else if (kind < 2.0) {\n"                                          /* dots */
    "    if (length(vec2((fract(x) - 0.5) * 1.2 * cos(lat), f - 0.5)) < 0.18) c = vec3(0.98);\n"
    "  } else {\n"                                                          /* wavy line */
    "    if (abs(f - 0.5 - 0.2 * sin(lon * 8.0)) < 0.07) c = pastel(bi + 3.0) * 0.85;\n"
    "  }\n"
    "  c *= 0.85 + 0.15 * smoothstep(0.0, 0.06, min(f, 1.0 - f));\n"        /* band edges */
    "  return vec4(lit(c, view, 40.0, 0.4) * u_bright, 1.0);\n"
    "}\n";

/* ---- 7: spring blossom ---- */
static const char BLOSSOM[] =
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  vec2 p = plane(view);\n"
    "  vec3 c = mix(vec3(0.55, 0.75, 0.95), vec3(0.95, 0.85, 0.90), 0.5 - 0.5 * p.y);\n"
    "  for (int k = 0; k < 3; k++) {\n"
    "    float fk = float(k), sc = (4.0 + fk * 3.0) * u_scale;\n"
    "    vec2 q = p * sc + vec2(u_t * 0.15 * (1.0 + fk * 0.3), u_t * (0.25 + fk * 0.1));\n"
    "    q.x += 0.3 * sin(q.y * 1.3 + u_t * 0.5);\n"                       /* sway */
    "    vec2 cell = floor(q), f = fract(q) - 0.5;\n"
    "    float h = h3(vec3(cell, fk + 7.0));\n"
    "    if (h > 0.45) {\n"
    "      vec2 r = rot(f - (vec2(h, fract(h * 5.3)) - 0.5) * 0.4, u_t * (h - 0.5) * 3.0 + h * 9.0);\n"
    "      float d = length(r * vec2(1.0, 1.8));\n"                       /* oval petal with a notch */
    "      float notch = step(0.0, r.x) * step(abs(r.y), 0.03 * (1.0 + r.x * 5.0));\n"
    "      if (d < 0.22 - fk * 0.03 && notch < 0.5) c = mix(vec3(1.0, 0.70, 0.82), vec3(1.0, 0.95, 0.97), fract(h * 3.7)) * (1.0 - fk * 0.08);\n"
    "    }\n"
    "  }\n"
    "  return vec4(c * u_bright, 1.0);\n"
    "}\n";

/* ---- 8: New Year (the countdown text is dots on top) ---- */
static const char NEWYEAR[] =
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  vec2 p = plane(view);\n"
    "  vec3 c = mix(vec3(0.02, 0.02, 0.06), vec3(0.10, 0.04, 0.16), 0.5 + 0.5 * p.y);\n"
    "  for (int k = 0; k < 3; k++) {\n"                                     /* gold confetti */
    "    float fk = float(k), sc = (7.0 + fk * 4.0) * u_scale;\n"
    "    vec2 q = p * sc + vec2(sin(u_t * 0.4 + fk) * 0.4, u_t * (0.6 + fk * 0.2));\n"
    "    vec2 cell = floor(q), f = fract(q) - 0.5;\n"
    "    float h = h3(vec3(cell, fk + 3.0));\n"
    "    if (h > 0.6) {\n"
    "      vec2 r = rot(f, u_t * 3.0 * (h - 0.5) + h * 7.0);\n"
    "      float spin = abs(sin(u_t * 4.0 * h + h * 20.0));\n"            /* catching the light */
    "      if (abs(r.x) < 0.10 && abs(r.y) < 0.05 * spin + 0.01) c = mix(vec3(1.0, 0.80, 0.30), vec3(0.95, 0.95, 1.0), step(0.85, h)) * (0.5 + 0.5 * spin);\n"
    "    }\n"
    "  }\n"
    "  return vec4(c * u_bright, 1.0);\n"
    "}\n";

/* ---- 9: autumn leaves ---- */
static const char LEAVES[] =
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  vec2 p = plane(view);\n"
    "  vec3 c = mix(vec3(0.95, 0.75, 0.45), vec3(0.55, 0.30, 0.20), 0.5 - 0.5 * p.y);\n"
    "  for (int k = 0; k < 3; k++) {\n"
    "    float fk = float(k), sc = (3.5 + fk * 2.5) * u_scale;\n"
    "    vec2 q = p * sc + vec2(0.0, u_t * (0.35 + fk * 0.12));\n"
    "    q.x += 0.35 * sin(q.y * 1.1 + u_t * 0.7 + fk);\n"
    "    vec2 cell = floor(q), f = fract(q) - 0.5;\n"
    "    float h = h3(vec3(cell, fk + 11.0));\n"
    "    if (h > 0.5) {\n"
    "      vec2 r = rot(f, sin(u_t * (1.0 + h) + h * 9.0) * 1.2 + h * 6.0) / (0.36 - fk * 0.04);\n"
    "      float a = atan(r.y, r.x), d = length(r);\n"
    "      float edge = 0.55 + 0.30 * pow(abs(cos(a * 2.5)), 3.0) - 0.12 * abs(sin(a * 7.5));\n"  /* five-lobed */
    "      if (d < edge) {\n"
    "        float ci = fract(h * 7.7) * 4.0;\n"
    "        vec3 lc = ci < 1.0 ? vec3(0.85, 0.20, 0.05) : ci < 2.0 ? vec3(0.95, 0.50, 0.05)\n"
    "                : ci < 3.0 ? vec3(0.95, 0.75, 0.15) : vec3(0.55, 0.30, 0.10);\n"
    "        float vein = smoothstep(0.04, 0.0, abs(r.y)) + smoothstep(0.03, 0.0, abs(abs(r.x) - abs(r.y) * 0.9) * step(0.0, r.x));\n"
    "        c = lc * (1.0 - fk * 0.12) * (1.0 - 0.25 * min(vein, 1.0));\n"
    "      }\n"
    "    }\n"
    "  }\n"
    "  return vec4(c * u_bright, 1.0);\n"
    "}\n";

static const char *GLSL[NSTYLE] = {
    NIGHT, BAUBLE, SNOWFALL, LIGHTS, PUMPKIN, SPOOKY, EGG, BLOSSOM, NEWYEAR, LEAVES
};
static GLuint g_prog[NSTYLE];
static float  g_t;
static DomePoints g_dots;

/* ---------------------------------------------------------------- */
/* Fireworks: a handful of rockets, each climbing then bursting      */
/* ---------------------------------------------------------------- */
#define NBURST 6
#define NSPARK 90
typedef struct { float t0, x, y, col[3]; float vx[NSPARK], vy[NSPARK]; } Burst;
static Burst g_b[NBURST];

static float rnd(void) { return (float)rand() / RAND_MAX; }

static void burst_new(Burst *b, float now)
{
    static const float C[6][3] = { {1,0.3f,0.2f}, {0.3f,1,0.4f}, {0.4f,0.6f,1},
                                   {1,0.85f,0.3f}, {1,0.4f,0.9f}, {1,1,1} };
    b->t0 = now + rnd() * 1.5f;
    b->x = (rnd() - 0.5f) * 1.1f;
    b->y = 0.05f + rnd() * 0.45f;
    memcpy(b->col, C[rand() % 6], sizeof b->col);
    float speed = 0.25f + rnd() * 0.15f;
    for (int i = 0; i < NSPARK; i++) {
        float a = rnd() * 6.2832f, s = speed * (0.6f + 0.4f * rnd());
        b->vx[i] = cosf(a) * s; b->vy[i] = sinf(a) * s;
    }
}

static void plate_dot(float x, float y, float r, float g, float b, float a)
{
    float rr = x * x + y * y;
    if (rr > 0.97f) return;
    dome_points_push(&g_dots, x, y, sqrtf(1.0f - rr), r, g, b, a);
}

static void fireworks(float now)
{
    const float RISE = 1.0f, LIFE = 2.4f;
    for (int k = 0; k < NBURST; k++) {
        Burst *b = &g_b[k];
        if (b->t0 == 0.0f || now > b->t0 + RISE + LIFE) burst_new(b, now);
        float t = now - b->t0;
        if (t < 0) continue;
        if (t < RISE) {                                   /* the rocket climbing */
            float y = -0.7f + (b->y + 0.7f) * (t / RISE);
            plate_dot(b->x, y, 1.0f, 0.8f, 0.5f, 1.0f);
            plate_dot(b->x, y - 0.02f, 1.0f, 0.6f, 0.3f, 0.5f);
            continue;
        }
        t -= RISE;
        float fade = 1.0f - t / LIFE;
        for (int i = 0; i < NSPARK; i++)
            for (int tr = 0; tr < 3; tr++) {               /* a short trail */
                float tt = fmaxf(0.0f, t - tr * 0.05f);
                float x = b->x + b->vx[i] * tt * (1.0f - tt * 0.25f);
                float y = b->y + b->vy[i] * tt * (1.0f - tt * 0.25f) - 0.06f * tt * tt;
                float tw = (i + tr) % 7 == 0 ? 0.5f + 0.5f * sinf(now * 30.0f + i) : 1.0f;
                plate_dot(x, y, b->col[0], b->col[1], b->col[2], fade * fade * tw * (1.0f - tr * 0.3f));
            }
    }
}

/* ---------------------------------------------------------------- */
/* New Year: days, or on New Year's Eve hours, to go                 */
/* ---------------------------------------------------------------- */
static void newyear_text(void)
{
    time_t now = time(NULL);
    struct tm lt = *localtime(&now);
    char l1[48], l2[48];
    const char *lines[2] = { l1, l2 };
    static const float col[2][3] = { { 1.0f, 0.85f, 0.35f }, { 0.95f, 0.95f, 1.0f } };
    if (lt.tm_mon == 0 && lt.tm_mday == 1) {
        snprintf(l1, sizeof l1, "HAPPY NEW YEAR");
        snprintf(l2, sizeof l2, "%d", lt.tm_year + 1900);
    } else {
        struct tm ny = { 0 };
        ny.tm_year = lt.tm_year + 1; ny.tm_mon = 0; ny.tm_mday = 1; ny.tm_isdst = -1;
        long secs = (long)difftime(mktime(&ny), now);
        snprintf(l1, sizeof l1, "%d", lt.tm_year + 1901);
        if (secs < 86400)
            snprintf(l2, sizeof l2, "IN %02ld:%02ld:%02ld", secs / 3600, secs / 60 % 60, secs % 60);
        else
            snprintf(l2, sizeof l2, "IN %ld DAYS", (secs + 86399) / 86400);
    }
    dome_text_block(&g_dots, lines, col, 2, 9.0f, NULL);
}

/* the style for today, when auto is on */
static int style_for_date(void)
{
    time_t now = time(NULL);
    struct tm lt = *localtime(&now);
    int m = lt.tm_mon + 1, d = lt.tm_mday;
    if ((m == 12 && d == 31) || (m == 1 && d == 1)) return 8;
    if (m == 12) return (int[]){ 1, 3, 2 }[d % 3];
    if (m == 1 || m == 2) return 2;
    if (m == 3) return 7;
    if (m == 4) return 6;
    if (m >= 5 && m <= 8) return 7;
    if (m == 10 && d >= 25) return d % 2 ? 4 : 5;
    if (m == 11 && d <= 10) return 0;
    return 9;
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
    dome_points_init(&g_dots, 60000);
}

static int style(void)
{
    if (L.autopick > 0.5f) return style_for_date();
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
    if (s == 0) {
        dome_points_clear(&g_dots);
        fireworks(g_t);
        dome_points_upload(&g_dots);
        mat3 still; m3_identity(still);
        dome_draw_points(&g_dots, still, f->fade, 3.0f);
    } else if (s == 8) {
        dome_points_clear(&g_dots);
        newyear_text();
        dome_points_upload(&g_dots);
        mat3 r; dome_view_rot(r);
        dome_draw_points(&g_dots, r, f->fade, 3.0f);
    }
}

static int key(SDL_Keycode k, int shift)
{
    (void)shift;
    if (k != SDLK_n) return 0;
    int s = style();
    L.autopick = 0.0f;
    L.style = (float)((s + 1) % NSTYLE);
    printf("Seasons: %s\n", NAMES[style()]);
    return 1;
}

Scene scene_seasons = {
    "SEASONS", "seasons.cfg", 0, P, (int)(sizeof P / sizeof P[0]),
    init, NULL, update, draw, key,
    "N next style (auto off)", NULL
};
