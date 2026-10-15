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
 *  10 puzzle cube  a plastic puzzle cube (2x2 up to 5x5, the size
 *                  setting) blown up into a ball: it starts mixed up,
 *                  solves itself a quarter turn at a time, then mixes
 *                  itself up again
 *  11 megaminx     the twelve-sided puzzle, the same way
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

#define NSTYLE 12
static const char *NAMES[NSTYLE] = {
    "football", "basketball", "tennis ball", "cricket ball", "baseball",
    "volleyball", "golf ball", "pool 8-ball", "beach ball", "bowling ball",
    "puzzle cube", "megaminx"
};

static struct { float style, roll, bright, shine, size; } L = { 0.0f, 0.3f, 1.0f, 1.0f, 3.0f };

static Param P[] = {
    { "style",  &L.style,  1.0f, 0.0f, 11.0f, 0,
      "0 football, 1 basketball, 2 tennis, 3 cricket, 4 baseball, 5 volleyball, "
      "6 golf, 7 pool 8-ball, 8 beach ball, 9 bowling ball, 10 puzzle cube, 11 megaminx (N)" },
    { "roll",   &L.roll,   0.05f, 0.0f, 3.0f, 0, "tumbling end over end, on top of the spin (0 = off)" },
    { "bright", &L.bright, 0.05f, 0.2f, 2.0f, 0, "brightness" },
    { "shine",  &L.shine,  0.1f, 0.0f, 2.0f, 0, "strength of the highlight" },
    { "size",   &L.size,   1.0f, 1.0f, 5.0f, 0,
      "puzzle cube layers: 2 to 5 (2x2 to 5x5), or 1 for a different size each time it mixes" },
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

/* ---- 10: puzzle cube, 2x2 to 5x5. N layers each way: planes cut the
 * sphere at equal angles across each face, giving 6 x N x N stickers.
 * Glossy stickers with rounded corners on black plastic; while a layer
 * turns, points in it look up the sticker they came from, turned back by
 * the current angle, and the slots either side of it open up. ---- */
#define PUZZLE_LIB \
    "uniform vec3 u_axis;\n"                       /* turning: u_lo < dot(d, axis) <= u_hi */ \
    "uniform float u_lo, u_hi, u_turn, u_glow;\n" \
    "vec3 turned(vec3 d, out float turning, out float seam){\n" \
    "  float x = dot(d, u_axis);\n" \
    "  turning = step(0.0001, abs(u_turn)) * step(u_lo, x) * step(x, u_hi);\n" \
    "  seam = step(0.0001, abs(u_turn)) > 0.5 ? min(abs(x - u_lo), abs(x - u_hi)) : 1.0;\n" \
    "  if (turning < 0.5) return d;\n" \
    "  float c = cos(-u_turn), s = sin(-u_turn);\n" \
    "  return d * c + cross(u_axis, d) * s + u_axis * dot(u_axis, d) * (1.0 - c);\n" \
    "}\n" \
    "vec3 finish(vec3 c, float on, float edge, float seam, vec3 view){\n" \
    "  c *= 0.82 + 0.18 * smoothstep(0.0, 0.025, edge);\n"      /* the sticker's rolled edge */ \
    "  vec3 plastic = vec3(0.025, 0.025, 0.03);\n" \
    "  vec3 col = mix(lit(plastic, view, 18.0, 0.25), lit(c, view, 70.0, 0.75), on);\n" \
    "  col *= smoothstep(0.003, 0.014, seam);\n"                  /* the open slot */ \
    "  return col * (1.0 + u_glow * 0.18 * (0.5 + 0.5 * sin(u_t * 6.0)));\n" \
    "}\n"

static const char CUBE[] =
    PUZZLE_LIB
    "uniform sampler2D u_cube;\n"                  /* sticker colours, 6 x N x N in a row */
    "uniform float u_n;\n"                         /* layers each way */
    "uniform vec4 u_cut;\n"                        /* the N-1 cuts, lowest first */
    "vec3 sticker(float i){\n"
    "  if (i < 0.5) return vec3(0.92, 0.92, 0.88);\n"     /* white  */
    "  if (i < 1.5) return vec3(1.00, 0.82, 0.00);\n"     /* yellow */
    "  if (i < 2.5) return vec3(0.78, 0.04, 0.06);\n"     /* red    */
    "  if (i < 3.5) return vec3(1.00, 0.40, 0.00);\n"     /* orange */
    "  if (i < 4.5) return vec3(0.00, 0.25, 0.72);\n"     /* blue   */
    "  return vec3(0.00, 0.58, 0.24);\n"                  /* green  */
    "}\n"
    "float cutk(float k){ return k < 0.5 ? u_cut.x : k < 1.5 ? u_cut.y : k < 2.5 ? u_cut.z : u_cut.w; }\n"
    "float band(float x){\n"
    "  float c = 0.0;\n"
    "  for (int k = 0; k < 4; k++) if (float(k) < u_n - 1.0 && x > cutk(float(k))) c += 1.0;\n"
    "  return c;\n"
    "}\n"
    "float edge(float s, float i, float n){\n"     /* distance to the sticker's edges one way */
    "  float lo = i < 0.5 ? (n - abs(s)) * 0.7071 : s - cutk(i - 1.0);\n"
    "  float hi = i > u_n - 1.5 ? (n - abs(s)) * 0.7071 : cutk(i) - s;\n"
    "  return min(lo, hi);\n"
    "}\n"
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  float turning, seam;\n"
    "  vec3 d = turned(ball(dir), turning, seam);\n"
    "  vec3 ad = abs(d);\n"
    "  float face, n; vec2 st;\n"
    "  if (ad.x >= ad.y && ad.x >= ad.z) { face = d.x > 0.0 ? 0.0 : 1.0; st = d.yz; n = ad.x; }\n"
    "  else if (ad.y >= ad.z) { face = d.y > 0.0 ? 2.0 : 3.0; st = d.zx; n = ad.y; }\n"
    "  else { face = d.z > 0.0 ? 4.0 : 5.0; st = d.xy; n = ad.z; }\n"
    "  float i = band(st.x), j = band(st.y);\n"
    "  float idx = face * u_n * u_n + j * u_n + i;\n"
    "  float v = texture2D(u_cube, vec2((idx + 0.5) / (6.0 * u_n * u_n), 0.5)).r * 255.0 / 20.0;\n"
    "  vec2 q = vec2(edge(st.x, i, n), edge(st.y, j, n)) - 0.06 / u_n;\n"
    "  float rad = 0.12 / u_n;\n"
    "  float sd = min(min(q.x, q.y), rad - length(max(vec2(rad) - q, 0.0)));\n"
    "  vec3 c = sticker(floor(v + 0.5)) * (0.97 + 0.03 * h3(floor(d * 400.0)));\n"
    "  return vec4(finish(c, smoothstep(0.0, 0.005, sd), sd, seam, view) * u_bright, 1.0);\n"
    "}\n";

/* ---- 11: megaminx. Twelve five-sided faces (the face centres are the
 * corners of an icosahedron). A point belongs to the face it is nearest
 * to. Seen flat from its face's centre, each face has five straight cut
 * lines, one parallel to each edge: a point past none of them is on the
 * centre piece, past one on an edge piece, past two on a corner piece.
 * Turning face j turns its own face and the strips past the j-side lines
 * of its five neighbours.
 * Working that out for every pixel was far too slow on the Pi 3, so it is
 * done once at start-up into two maps of the sphere: which sticker each
 * point is on, and how far it is from the sticker's edge. Each frame is
 * then a few texture reads. ---- */
#define MEGA_R 0.32f
#define MEGA_W 1024
#define MEGA_H 512
static const char MEGA[] =
    "uniform vec3 u_axis;\n"
    "uniform float u_turn, u_glow, u_tf;\n"         /* u_tf: the face turning, -1 none */
    "uniform sampler2D u_mid, u_mdist, u_mcol, u_mlayer;\n"
    "vec3 sticker(float i){\n"
    "  if (i < 0.5) return vec3(0.92, 0.92, 0.88);\n"
    "  if (i < 1.5) return vec3(1.00, 0.82, 0.00);\n"
    "  if (i < 2.5) return vec3(0.80, 0.05, 0.07);\n"
    "  if (i < 3.5) return vec3(1.00, 0.45, 0.00);\n"
    "  if (i < 4.5) return vec3(0.00, 0.22, 0.70);\n"
    "  if (i < 5.5) return vec3(0.00, 0.55, 0.20);\n"
    "  if (i < 6.5) return vec3(0.45, 0.15, 0.65);\n"     /* purple      */
    "  if (i < 7.5) return vec3(1.00, 0.55, 0.70);\n"     /* pink        */
    "  if (i < 8.5) return vec3(0.55, 0.85, 0.20);\n"     /* lime        */
    "  if (i < 9.5) return vec3(0.35, 0.75, 1.00);\n"     /* light blue  */
    "  if (i < 10.5) return vec3(0.95, 0.85, 0.60);\n"    /* cream       */
    "  return vec3(0.45, 0.45, 0.48);\n"                  /* grey        */
    "}\n"
    "vec2 eq(vec3 d){ return vec2(atan(d.x, d.z) / (2.0 * PI) + 0.5, 0.5 - asin(clamp(d.y, -1.0, 1.0)) / PI); }\n"
    "float sid(vec3 d){ return floor(texture2D(u_mid, eq(d)).r * 255.0 + 0.5); }\n"
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  vec3 d = ball(dir);\n"
    "  if (u_tf > -0.5) {\n"                          /* in the turning layer? */
    "    float s0 = sid(d);\n"
    "    if (texture2D(u_mlayer, vec2((s0 + 0.5) / 132.0, (u_tf + 0.5) / 12.0)).r > 0.5) {\n"
    "      float c = cos(-u_turn), s = sin(-u_turn);\n"
    "      d = d * c + cross(u_axis, d) * s + u_axis * dot(u_axis, d) * (1.0 - c);\n"
    "    }\n"
    "  }\n"
    "  float st = sid(d);\n"
    "  float v = texture2D(u_mcol, vec2((st + 0.5) / 132.0, 0.5)).r * 255.0 / 20.0;\n"
    "  float sd = texture2D(u_mdist, eq(d)).r * 0.08 - 0.011;\n"
    "  vec3 c = sticker(floor(v + 0.5)) * (0.97 + 0.03 * h3(floor(d * 400.0)));\n"
    "  c *= 0.82 + 0.18 * smoothstep(0.0, 0.025, sd);\n"
    "  vec3 plastic = vec3(0.025, 0.025, 0.03);\n"
    "  vec3 col = mix(lit(plastic, view, 18.0, 0.25), lit(c, view, 70.0, 0.75), smoothstep(0.0, 0.005, sd));\n"
    "  col *= 1.0 + u_glow * 0.18 * (0.5 + 0.5 * sin(u_t * 6.0));\n"
    "  return vec4(col * u_bright, 1.0);\n"
    "}\n";

static const char *GLSL[NSTYLE] = {
    FOOTBALL, BASKETBALL, TENNIS, CRICKET, BASEBALL, VOLLEY, GOLF, POOL, BEACH, BOWLING, CUBE, MEGA
};
static GLuint g_prog[NSTYLE];
static float  g_roll;

/* ---------------------------------------------------------------- */
/* Puzzles (the cube, 2x2 to 5x5, and the megaminx). Each sticker is  */
/* found by the same rules the shaders use (classify); a sample of    */
/* points over the sphere gives each sticker a middle; a move turns   */
/* everything inside a slab about an axis, and where each middle      */
/* lands gives the move's permutation. To "solve", a puzzle plays its */
/* mixing moves back in reverse.                                      */
/* ---------------------------------------------------------------- */
#define MAXST 160
#define MAXMV 32
#define MAXMIX 44
typedef struct {
    int   kind;                       /* 0 cube, 1 megaminx */
    int   n;                          /* cube layers each way */
    float cut[4];
    int   nst, nmv;
    float mid[MAXST][3];
    unsigned char col[MAXST];
    int   key2st[12 * 169];           /* megaminx: lookup key -> sticker */
    float ax[MAXMV][3], lo[MAXMV], hi[MAXMV], ang[MAXMV];
    short perm[MAXMV][MAXST];         /* new[i] = old[perm[m][i]] */
    int   mix[MAXMIX], nmix;
    int   phase, step, move;          /* phases: 0 mixing, 1 pause, 2 solving, 3 solved */
    float t, wait;
} Puzzle;

static Puzzle g_pz[2];
static GLuint g_pz_tex[2];
static GLuint g_mid_tex, g_mdist_tex, g_mlayer_tex;     /* the megaminx's maps */
static float  g_mc[12][3];            /* megaminx face centres */
static int    g_cube_n;               /* the size the cube was built at */

static void norm3(float v[3]) { float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); v[0] /= l; v[1] /= l; v[2] /= l; }

static void rot_axis(const float a[3], float ang, const float v[3], float out[3])
{
    float c = cosf(ang), s = sinf(ang), d = a[0] * v[0] + a[1] * v[1] + a[2] * v[2];
    float x[3] = { a[1] * v[2] - a[2] * v[1], a[2] * v[0] - a[0] * v[2], a[0] * v[1] - a[1] * v[0] };
    for (int k = 0; k < 3; k++) out[k] = v[k] * c + x[k] * s + a[k] * d * (1 - c);
}

static float dot3(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

static int mega_face(const float d[3])
{
    int f = 0; float best = -2;
    for (int k = 0; k < 12; k++) { float v = dot3(d, g_mc[k]); if (v > best) { best = v; f = k; } }
    return f;
}

static int mega_nb(int f, int k) { float c = dot3(g_mc[f], g_mc[k]); return c > 0.3f && c < 0.99f; }

/* how far d is past face f's cut line towards neighbour k (as the shader) */
static float mega_past(const float d[3], int f, int k)
{
    float c = dot3(g_mc[k], g_mc[f]), u[3];
    for (int i = 0; i < 3; i++) u[i] = g_mc[k][i] - g_mc[f][i] * c;
    norm3(u);
    return dot3(d, u) - MEGA_R * dot3(d, g_mc[f]);
}

/* is d in the layer that turns with face j? */
static int mega_in(const float d[3], int j)
{
    int f = mega_face(d);
    return f == j || (mega_nb(f, j) && mega_past(d, f, j) >= 0);
}

static int band(const Puzzle *p, float x)
{
    int c = 0;
    for (int k = 0; k < p->n - 1; k++) if (x > p->cut[k]) c++;
    return c;
}

/* which sticker a direction is on: the same rules as the shaders */
static int classify(const Puzzle *p, const float d[3])
{
    if (p->kind == 0) {
        float ad[3] = { fabsf(d[0]), fabsf(d[1]), fabsf(d[2]) }, s, t;
        int face;
        if (ad[0] >= ad[1] && ad[0] >= ad[2]) { face = d[0] > 0 ? 0 : 1; s = d[1]; t = d[2]; }
        else if (ad[1] >= ad[2]) { face = d[1] > 0 ? 2 : 3; s = d[2]; t = d[0]; }
        else { face = d[2] > 0 ? 4 : 5; s = d[0]; t = d[1]; }
        return face * p->n * p->n + band(p, t) * p->n + band(p, s);
    }
    int f = mega_face(d), a = -1, b = -1;
    for (int k = 0; k < 12; k++) {
        if (!mega_nb(f, k)) continue;
        if (mega_past(d, f, k) > 0) { if (a < 0) a = k; else b = k; }
    }
    int lo = a, hi = b;
    return f * 169 + (lo + 1) * 13 + (hi + 1);
}

static int sticker_of(const Puzzle *p, const float d[3])
{
    int k = classify(p, d);
    return p->kind == 0 ? k : p->key2st[k];
}

static void puzzle_build(Puzzle *p, int kind, int n)
{
    static float acc[12 * 169][4];
    memset(p, 0, sizeof *p);
    p->kind = kind; p->n = n; p->move = -1;
    for (int k = 0; k < n - 1; k++) p->cut[k] = sinf(DEG2RAD(-45.0f + 90.0f * (k + 1) / n));
    memset(acc, 0, sizeof acc);
    /* sample the sphere evenly and average each sticker's points */
    const int NS = 60000;
    for (int i = 0; i < NS; i++) {
        float y = 1.0f - 2.0f * (i + 0.5f) / NS, r = sqrtf(1.0f - y * y), a = i * 2.39996323f;
        float d[3] = { r * cosf(a), y, r * sinf(a) };
        int k = classify(p, d);
        acc[k][0] += d[0]; acc[k][1] += d[1]; acc[k][2] += d[2]; acc[k][3] += 1;
    }
    int nkeys = kind == 0 ? 6 * n * n : 12 * 169;
    for (int k = 0; k < 12 * 169; k++) p->key2st[k] = 0;
    for (int k = 0; k < nkeys && p->nst < MAXST; k++) {
        if (acc[k][3] < 3) continue;
        int s = kind == 0 ? k : p->nst;
        if (kind == 1) p->key2st[k] = s;
        for (int c = 0; c < 3; c++) p->mid[s][c] = acc[k][c];
        norm3(p->mid[s]);
        p->col[s] = (unsigned char)(kind == 0 ? k / (n * n) : k / 169);
        if (kind == 0) { if (s + 1 > p->nst) p->nst = s + 1; } else p->nst++;
    }
    /* the moves */
    if (kind == 0) {
        for (int a = 0; a < 3; a++)
            for (int l = 0; l < n; l++)
                for (int dir = 0; dir < 2; dir++) {
                    int m = p->nmv++;
                    p->ax[m][0] = p->ax[m][1] = p->ax[m][2] = 0;
                    p->ax[m][a] = 1;
                    p->lo[m] = l == 0 ? -2.0f : p->cut[l - 1];
                    p->hi[m] = l == n - 1 ? 2.0f : p->cut[l];
                    p->ang[m] = dir ? -1.5708f : 1.5708f;
                }
    } else {
        for (int f = 0; f < 12; f++)
            for (int dir = 0; dir < 2; dir++) {
                int m = p->nmv++;
                memcpy(p->ax[m], g_mc[f], sizeof g_mc[f]);
                p->ang[m] = dir ? -1.2566f : 1.2566f;
            }
    }
    for (int m = 0; m < p->nmv; m++) {
        for (int i = 0; i < p->nst; i++) p->perm[m][i] = (short)i;
        for (int s = 0; s < p->nst; s++) {
            if (p->kind == 0) {
                float x = dot3(p->mid[s], p->ax[m]);
                if (x <= p->lo[m] || x > p->hi[m]) continue;
            } else if (!mega_in(p->mid[s], m / 2)) continue;
            float q[3];
            rot_axis(p->ax[m], p->ang[m], p->mid[s], q);
            p->perm[m][sticker_of(p, q)] = (short)s;
        }
    }
}

static void puzzle_apply(Puzzle *p, int m)
{
    unsigned char old[MAXST];
    memcpy(old, p->col, sizeof old);
    for (int i = 0; i < p->nst; i++) p->col[i] = old[p->perm[m][i]];
}

static void puzzle_mix(Puzzle *p, int instant)
{
    p->nmix = p->kind == 1 ? 34 : p->n == 2 ? 14 : p->n == 3 ? 22 : p->n == 4 ? 32 : 40;
    for (int k = 0; k < p->nmix; k++) {
        int m;
        do m = rand() % p->nmv; while (k && (m ^ 1) == p->mix[k - 1]);   /* never undo the last move */
        p->mix[k] = m;
        if (instant) puzzle_apply(p, m);
    }
}

static void puzzle_step(Puzzle *p, float dt)
{
    const float DUR[4] = { p->kind == 1 ? 0.16f : 0.12f, 1.8f, 0.45f, 4.0f };
    if (p->phase == 1 || p->phase == 3) {
        p->wait += dt;
        if (p->wait > DUR[p->phase]) {
            p->wait = 0; p->step = 0;
            if (p->phase == 3) puzzle_mix(p, 0);
            p->phase = (p->phase + 1) % 4;
        }
        return;
    }
    if (p->move < 0) {
        if (p->step >= p->nmix) { p->phase++; p->wait = 0; return; }
        p->move = p->phase == 0 ? p->mix[p->step] : p->mix[p->nmix - 1 - p->step] ^ 1;
        p->t = 0;
    }
    p->t += dt / DUR[p->phase];
    if (p->t >= 1.0f) { puzzle_apply(p, p->move); p->move = -1; p->step++; p->t = 0; }
}

static void puzzle_upload(int which)
{
    Puzzle *p = &g_pz[which];
    glBindTexture(GL_TEXTURE_2D, g_pz_tex[which]);
    if (p->kind == 0) {
        unsigned char px[MAXST];
        for (int i = 0; i < p->nst; i++) px[i] = (unsigned char)(p->col[i] * 20);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, p->nst, 1, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, px);
    } else {
        unsigned char px[132] = { 0 };
        for (int i = 0; i < p->nst && i < 132; i++) px[i] = (unsigned char)(p->col[i] * 20);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, 132, 1, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, px);
    }
}

/* the cube's size: the setting, or a new random size for each mix */
static void cube_size_check(void)
{
    int want = (int)lroundf(L.size);
    if (want >= 2 && want <= 5) {
        if (want == g_cube_n) return;
    } else {
        if (g_cube_n && !(g_pz[0].phase == 3 && g_pz[0].wait + 0.05f > 4.0f)) return;
        want = 2 + rand() % 4;                        /* "any": pick one each time it re-mixes */
        if (want == g_cube_n) want = want == 5 ? 2 : want + 1;
    }
    g_cube_n = want;
    puzzle_build(&g_pz[0], 0, want);
    puzzle_mix(&g_pz[0], 1);                          /* it starts mixed up, then solves itself */
    g_pz[0].phase = 1;
}

static GLuint tex_new(int linear)
{
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

/* The megaminx maps, built once: for each point of an equirectangular map
 * (lon = atan(x, z), lat = asin(y), as the shader reads it), the sticker
 * it is on, and its distance from that sticker's edge; and for each
 * sticker, which of the twelve turning layers it belongs to. */
static void mega_maps(const Puzzle *p)
{
    static unsigned char id[MEGA_H][MEGA_W], dist[MEGA_H][MEGA_W], layer[12][132];
    for (int y = 0; y < MEGA_H; y++) {
        float lat = (0.5f - (y + 0.5f) / MEGA_H) * DOME_PI;
        for (int x = 0; x < MEGA_W; x++) {
            float lon = ((x + 0.5f) / MEGA_W - 0.5f) * 2.0f * DOME_PI;
            float d[3] = { cosf(lat) * sinf(lon), sinf(lat), cosf(lat) * cosf(lon) };
            id[y][x] = (unsigned char)sticker_of(p, d);
            /* distance to the edge: to the next face, and to the cut lines */
            int f = mega_face(d);
            float best = dot3(d, g_mc[f]), second = -2, cut = 1;
            for (int k = 0; k < 12; k++) {
                if (k == f) continue;
                float v = dot3(d, g_mc[k]);
                if (v > second) second = v;
                if (mega_nb(f, k)) cut = fminf(cut, fabsf(mega_past(d, f, k)));
            }
            float sd = fminf((best - second) * 1.1f, cut * 0.9f);
            dist[y][x] = (unsigned char)fminf(255.0f, sd / 0.08f * 255.0f);
        }
    }
    for (int j = 0; j < 12; j++)
        for (int s = 0; s < 132; s++) layer[j][s] = s < p->nst && mega_in(p->mid[s], j) ? 255 : 0;
    g_mid_tex = tex_new(0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, MEGA_W, MEGA_H, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, id);
    g_mdist_tex = tex_new(1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, MEGA_W, MEGA_H, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, dist);
    g_mlayer_tex = tex_new(0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, 132, 12, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, layer);
}

static void puzzle_init(void)
{
    const float PHI = 1.6180340f;
    float v[12][3] = { {0,1,PHI}, {0,-1,PHI}, {0,1,-PHI}, {0,-1,-PHI}, {1,PHI,0}, {-1,PHI,0},
                       {1,-PHI,0}, {-1,-PHI,0}, {PHI,0,1}, {-PHI,0,1}, {PHI,0,-1}, {-PHI,0,-1} };
    for (int i = 0; i < 12; i++) { memcpy(g_mc[i], v[i], sizeof v[i]); norm3(g_mc[i]); }
    for (int w = 0; w < 2; w++) {
        glGenTextures(1, &g_pz_tex[w]);
        glBindTexture(GL_TEXTURE_2D, g_pz_tex[w]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    cube_size_check();
    puzzle_build(&g_pz[1], 1, 0);
    puzzle_mix(&g_pz[1], 1);
    g_pz[1].phase = 1;
    mega_maps(&g_pz[1]);
}

static void puzzle_uniforms(GLuint prog, int which)
{
    Puzzle *p = &g_pz[which];
    float a[3] = { 0, 1, 0 }, ang = 0, lo = 0, hi = 0;
    if (p->move >= 0) {
        memcpy(a, p->ax[p->move], sizeof a);
        float e = p->t * p->t * (3.0f - 2.0f * p->t);              /* ease in and out */
        ang = p->ang[p->move] * e;
        lo = p->lo[p->move]; hi = p->hi[p->move];
    }
    glActiveTexture(GL_TEXTURE1);
    puzzle_upload(which);
    glUniform1i(glGetUniformLocation(prog, which ? "u_mcol" : "u_cube"), 1);
    if (which) {
        glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, g_mid_tex);
        glUniform1i(glGetUniformLocation(prog, "u_mid"), 2);
        glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, g_mdist_tex);
        glUniform1i(glGetUniformLocation(prog, "u_mdist"), 3);
        glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D, g_mlayer_tex);
        glUniform1i(glGetUniformLocation(prog, "u_mlayer"), 4);
    }
    glActiveTexture(GL_TEXTURE0);
    glUniform3f(glGetUniformLocation(prog, "u_axis"), a[0], a[1], a[2]);
    glUniform1f(glGetUniformLocation(prog, "u_turn"), ang);
    glUniform1f(glGetUniformLocation(prog, "u_lo"), lo);
    glUniform1f(glGetUniformLocation(prog, "u_hi"), hi);
    glUniform1f(glGetUniformLocation(prog, "u_glow"), p->phase == 3 ? 1.0f : 0.0f);
    if (which == 0) {
        glUniform1f(glGetUniformLocation(prog, "u_n"), (float)p->n);
        glUniform4f(glGetUniformLocation(prog, "u_cut"), p->cut[0], p->cut[1], p->cut[2], p->cut[3]);
    } else {
        glUniform1f(glGetUniformLocation(prog, "u_tf"), p->move >= 0 ? (float)(p->move / 2) : -1.0f);
    }
}

static void init(void)
{
    for (int i = 0; i < NSTYLE; i++) {
        size_t n = strlen(LIB) + strlen(GLSL[i]) + 1;
        char *src = malloc(n);
        snprintf(src, n, "%s%s", LIB, GLSL[i]);
        g_prog[i] = dome_surface_program_lib(DOME_GLSL_NOISE, src);
        free(src);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    puzzle_init();
}

static int style(void)
{
    int s = (int)lroundf(L.style);
    return s < 0 ? 0 : s >= NSTYLE ? NSTYLE - 1 : s;
}

static void update(const DomeFrame *f)
{
    g_roll = fmodf(g_roll + f->dt * L.roll, 6.2832f);
    float dt = fminf(f->dt, 0.1f);
    if (style() == 10) { cube_size_check(); puzzle_step(&g_pz[0], dt); }
    if (style() == 11) puzzle_step(&g_pz[1], dt);
}

static void draw(const DomeFrame *f)
{
    GLuint p = g_prog[style()];
    dome_surface_begin(p, f);
    glUniform1f(glGetUniformLocation(p, "u_t"), f->now);
    glUniform1f(glGetUniformLocation(p, "u_roll"), g_roll);
    glUniform1f(glGetUniformLocation(p, "u_bright"), L.bright);
    glUniform1f(glGetUniformLocation(p, "u_shine"), L.shine);
    if (style() == 10) puzzle_uniforms(p, 0);
    if (style() == 11) puzzle_uniforms(p, 1);
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
