/*
 * groovy2.c  -  Groovy 2: smileys and flower power. N steps through the styles:
 *
 *   0 smiley bounce    smiley faces bobbing, winking and grinning on bright tiles
 *   1 daisy chain      rings of swaying daisies on a vine, turning in turn
 *   2 flower wallpaper chunky 1967 poster flowers, colours swapping round
 *   3 blooming garden  flowers opening petal by petal, then closing again
 *   4 sunshine face    one big smiling sun with wavy rays and rainbow rings
 *   5 peace and petals peace symbols and flowers on tiles that flip over
 *   6 smiley rain      smileys and flowers drifting down onto a meadow
 *   7 flower kaleido   one flower mirrored twelve ways, petals breathing
 *   8 rainbow face     a smiley in the middle of a turning rainbow spiral
 *   9 hippie bus       two-tone panel with swirl stripes, flowers and smileys
 *
 * M picks the colours: 0 is each style's own, 1 to 8 the shared themes.
 * Styles 4, 6 and 8 face you; the rest turn with the ball.
 *
 * Each style is its own small shader, so only the one on screen is run,
 * and all of them are built from circles, petals and a few sines: no
 * noise, so they stay quick on the Pi 3.
 */
#include "../dome/dome.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NSTYLE 10
static const char *NAMES[NSTYLE] = {
    "smiley bounce", "daisy chain", "flower wallpaper", "blooming garden",
    "sunshine face", "peace and petals", "smiley rain", "flower kaleidoscope",
    "rainbow face", "hippie bus"
};

static struct { float style, theme, speed, scale, bright, wink; } L =
    { 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f };

static Param P[] = {
    { "style",  &L.style,  1.0f, 0.0f, 9.0f, 0,
      "0 smileys, 1 daisy chain, 2 wallpaper, 3 garden, 4 sunshine, 5 peace, "
      "6 smiley rain, 7 kaleidoscope, 8 rainbow face, 9 hippie bus (N)" },
    { "theme",  &L.theme,  1.0f, 0.0f, 8.0f, 0,
      "0 classic, 1 rainbow, 2 fire, 3 ice, 4 neon, 5 aurora, 6 gold, 7 amber, 8 jewel (M)" },
    { "speed",  &L.speed,  0.1f, 0.0f, 4.0f, 0, "speed of the animation" },
    { "scale",  &L.scale,  0.05f, 0.4f, 2.5f, 0, "size of the pattern (bigger = more, smaller)" },
    { "bright", &L.bright, 0.05f, 0.2f, 2.0f, 0, "brightness" },
    { "wink",   &L.wink,   0.1f, 0.0f, 3.0f, 0, "how often the smileys wink and blink (0 = never)" },
};

/* ---- shared by every style ---- */
static const char LIB[] =
    "uniform float u_t, u_scale, u_bright, u_theme, u_wink;\n"
    "vec2 rot(vec2 p, float a){ float c = cos(a), s = sin(a); return vec2(c * p.x - s * p.y, s * p.x + c * p.y); }\n"
    /* classic pop colours, or a shared theme when M picks one */
    "vec3 popc(float x){\n"
    "  if (u_theme > 0.5) return theme(u_theme - 1.0, x);\n"
    "  float i = mod(floor(x * 6.0), 6.0);\n"
    "  if (i < 1.0) return vec3(1.00, 0.20, 0.55);\n"
    "  if (i < 2.0) return vec3(1.00, 0.55, 0.05);\n"
    "  if (i < 3.0) return vec3(1.00, 0.88, 0.15);\n"
    "  if (i < 4.0) return vec3(0.45, 0.85, 0.20);\n"
    "  if (i < 5.0) return vec3(0.15, 0.75, 0.90);\n"
    "  return vec3(0.60, 0.25, 0.90);\n"
    "}\n"
    /* rows of roughly square cells round the globe, fewer near the poles.
     * Returns where this point sits in its cell (-0.5..0.5); id names the
     * cell. shift slides alternate rows opposite ways. */
    "vec2 cells(vec3 dir, float rows, float shift, out vec2 id){\n"
    "  float lat = asin(clamp(dir.y, -1.0, 1.0)), lon = atan(dir.x, dir.z);\n"
    "  float rf = (lat / PI + 0.5) * rows, row = floor(rf);\n"
    "  float latc = ((row + 0.5) / rows - 0.5) * PI;\n"
    "  float n = max(1.0, floor(rows * 2.0 * cos(latc) + 0.5));\n"
    "  float cf = (lon / (2.0 * PI) + 0.5) * n + shift * (mod(row, 2.0) * 2.0 - 1.0);\n"
    "  id = vec2(mod(floor(cf), n), row);\n"
    "  return vec2((fract(cf) - 0.5) * 2.0 * rows * cos(lat) / n, fract(rf) - 0.5);\n"
    "}\n"
    /* a smiley face of radius 1: rgb, and coverage in a */
    "vec4 smiley(vec2 p, float shutL, float shutR, float grin, vec3 face){\n"
    "  float d = length(p);\n"
    "  float cov = smoothstep(1.0, 0.94, d);\n"
    "  if (cov <= 0.0) return vec4(0.0);\n"
    "  vec3 c = face * (1.0 - 0.22 * smoothstep(0.5, 1.0, d));\n"
    "  c += vec3(0.3) * smoothstep(0.28, 0.0, length(p - vec2(-0.38, 0.48)));\n"   /* shine */
    "  float ink = smoothstep(0.86, 0.91, d);\n"
    "  vec2 el = p - vec2(-0.33, 0.28), er = p - vec2(0.33, 0.28);\n"
    "  float oL = max(1.0 - shutL, 0.12), oR = max(1.0 - shutR, 0.12);\n"
    "  ink = max(ink, smoothstep(1.15, 0.9, length(vec2(el.x / 0.11, el.y / (0.2 * oL)))));\n"
    "  ink = max(ink, smoothstep(1.15, 0.9, length(vec2(er.x / 0.11, er.y / (0.2 * oR)))));\n"
    "  vec2 m = p - vec2(0.0, 0.12);\n"
    "  float mr = length(m), rad = 0.55 + 0.04 * grin;\n"
    "  float lip = mix(-0.24, -0.04, grin);\n"
    "  float open = grin * smoothstep(rad + 0.02, rad - 0.02, mr) * smoothstep(lip + 0.02, lip - 0.02, m.y);\n"
    "  c = mix(c, vec3(0.45, 0.06, 0.10), open);\n"
    "  ink = max(ink, smoothstep(0.075, 0.045, abs(mr - rad)) * smoothstep(lip + 0.02, lip - 0.02, m.y));\n"
    "  ink = max(ink, grin * smoothstep(0.05, 0.02, abs(m.y - lip)) * step(mr, rad));\n"
    "  c = mix(c, vec3(0.06, 0.04, 0.03), ink);\n"
    "  return vec4(c, cov);\n"
    "}\n"
    /* a flower of radius about 1 with n petals: rgb, coverage in a */
    "vec4 flower(vec2 p, float n, float open, vec3 pc, vec3 cc){\n"
    "  float r = length(p), a = atan(p.y, p.x);\n"
    "  float edge = (0.38 + 0.62 * pow(abs(cos(a * n * 0.5)), 0.7)) * open;\n"
    "  float cov = smoothstep(edge + 0.04, edge - 0.04, r);\n"
    "  vec3 c = pc * (0.75 + 0.25 * smoothstep(edge, 0.2, r));\n"
    "  c *= 1.0 - 0.5 * smoothstep(0.07, 0.0, abs(r - edge + 0.03));\n"
    "  float cr = 0.28 * min(open * 1.5, 1.0);\n"
    "  float centre = smoothstep(cr + 0.03, cr - 0.03, r);\n"
    "  c = mix(c, cc * (0.8 + 0.2 * smoothstep(cr, 0.0, r)), centre);\n"
    "  return vec4(c, max(cov, centre));\n"
    "}\n"
    /* wink and blink timing for one face: x = wink (one eye), y = blink */
    "vec2 eyes(float h){\n"
    "  float w = fract(u_t * 0.12 * u_wink + h * 7.0);\n"
    "  float b = fract(u_t * 0.21 * u_wink + h * 5.0);\n"
    "  return vec2(smoothstep(0.0, 0.02, w) * smoothstep(0.07, 0.05, w),\n"
    "              smoothstep(0.0, 0.01, b) * smoothstep(0.035, 0.02, b)) * step(0.01, u_wink);\n"
    "}\n";

/* ---- 0: smiley bounce ---- */
static const char SMILEYS[] =
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  vec2 id; vec2 q = cells(dir, floor(7.0 * u_scale + 0.5), 0.0, id);\n"
    "  float h = h3(vec3(id, 1.0)), h2 = h3(vec3(id, 2.0));\n"
    "  vec3 c = popc(h + floor(u_t * 0.1 + h2) * 0.37);\n"            /* tiles change now and then */
    "  c *= 0.8 + 0.2 * smoothstep(0.5, 0.35, max(abs(q.x), abs(q.y)));\n"
    "  float bob = abs(sin(u_t * (1.6 + h) + h * 6.0)) * 0.12;\n"
    "  float sh = smoothstep(0.40, 0.28, length(q - vec2(0.04, -0.07)));\n"
    "  c *= 1.0 - sh * (0.35 - bob * 1.5);\n"                        /* shadow on the tile */
    "  vec2 e = eyes(h2);\n"
    "  float grin = smoothstep(0.6, 0.9, sin(u_t * 0.35 + h * 11.0));\n"
    "  vec4 s = smiley((q - vec2(0.0, bob - 0.04)) / 0.36, max(e.x, e.y), e.y, grin, vec3(1.0, 0.85, 0.1));\n"
    "  c = mix(c, s.rgb, s.a);\n"
    "  return vec4(c * u_bright, 1.0);\n"
    "}\n";

/* ---- 1: daisy chain ---- */
static const char DAISIES[] =
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  vec2 id; vec2 q = cells(dir, floor(5.0 * u_scale + 0.5), u_t * 0.05, id);\n"
    "  vec3 c = mix(vec3(0.50, 0.30, 0.80), vec3(0.95, 0.55, 0.75), 0.5 + 0.5 * dir.y);\n"
    "  if (u_theme > 0.5) c = popc(0.25 + 0.25 * dir.y) * 0.55;\n"
    "  float vine = smoothstep(0.04, 0.015, abs(q.y + 0.05 * sin(q.x * 6.2832 + u_t)));\n"
    "  c = mix(c, vec3(0.15, 0.60, 0.20), vine);\n"
    "  float sway = 0.3 * sin(u_t * 1.2 + id.x * 0.9 + id.y);\n"
    "  vec4 f = flower(rot(q, sway) / 0.42, 12.0, 1.0, vec3(1.0, 1.0, 0.96), vec3(1.0, 0.78, 0.10));\n"
    "  c = mix(c, f.rgb, f.a);\n"
    "  return vec4(c * u_bright, 1.0);\n"
    "}\n";

/* ---- 2: flower power wallpaper ---- */
static const char WALLPAPER[] =
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  vec2 id; vec2 q = cells(dir, floor(4.0 * u_scale + 0.5), 0.0, id);\n"
    "  float h = h3(vec3(id, 3.0));\n"
    "  float k = floor(u_t * 0.25), fd = smoothstep(0.85, 1.0, fract(u_t * 0.25));\n"
    "  float ck = mod(id.x + id.y, 2.0);\n"
    "  vec3 c = ck < 0.5 ? vec3(0.98, 0.90, 0.72) : vec3(0.35, 0.18, 0.08);\n"
    "  vec3 pc = popc(h + k / 6.0), cc = popc(h + 0.5 + k / 6.0);\n"
    "  if (fd > 0.0) {\n"                                             /* only while swapping */
    "    pc = mix(pc, popc(h + (k + 1.0) / 6.0), fd);\n"
    "    cc = mix(cc, popc(h + 0.5 + (k + 1.0) / 6.0), fd);\n"
    "  }\n"
    "  vec4 f = flower(rot(q, h * 6.2832) / 0.5, 5.0, 1.0, pc, cc);\n"
    "  c = mix(c, f.rgb, f.a);\n"
    "  return vec4(c * u_bright, 1.0);\n"
    "}\n";

/* ---- 3: blooming garden ---- */
static const char GARDEN[] =
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  vec2 id; vec2 q = cells(dir, floor(6.0 * u_scale + 0.5), 0.0, id);\n"
    "  float h = h3(vec3(id, 4.0)), h2 = h3(vec3(id, 5.0));\n"
    "  float ph = fract(u_t * 0.06 * (0.7 + 0.6 * h2) + h);\n"
    "  float bloom = smoothstep(0.05, 0.35, ph) * (1.0 - smoothstep(0.75, 0.95, ph));\n"
    "  vec3 c = vec3(0.05, 0.20, 0.07) * (0.8 + 0.2 * sin(q.x * 40.0 + q.y * 7.0 + h * 9.0));\n"
    "  vec2 p = rot(q, h * 6.2832) / 0.44;\n"
    "  float r = length(p), a = atan(p.y, p.x);\n"
    "  const float NP = 6.0;\n"
    "  float k = floor((a + PI) / (2.0 * PI) * NP);\n"                 /* which petal */
    "  float ac = (k + 0.5) / NP * 2.0 * PI - PI;\n"
    "  float open = clamp(bloom * (NP + 1.0) - k, 0.0, 1.0);\n"        /* opening in turn */
    "  vec2 pp = vec2(r * cos(a - ac), r * sin(a - ac));\n"
    "  float len = 0.25 + 0.75 * open, wid = 0.06 + 0.26 * open;\n"
    "  float e = length(vec2((pp.x - len * 0.5) / (len * 0.5), pp.y / wid));\n"
    "  vec3 pc = popc(h2);\n"
    "  c = mix(c, pc * (0.7 + 0.3 * smoothstep(1.0, 0.2, e)), smoothstep(1.08, 0.92, e) * step(0.01, open));\n"
    "  float bud = 0.10 + 0.14 * (1.0 - bloom);\n"
    "  c = mix(c, mix(vec3(0.25, 0.65, 0.20), vec3(1.0, 0.80, 0.15), bloom), smoothstep(bud + 0.03, bud - 0.03, r));\n"
    "  return vec4(c * u_bright, 1.0);\n"
    "}\n";

/* ---- 4: sunshine face (faces you) ---- */
static const char SUNSHINE[] =
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  float a = acos(clamp(view.z, -1.0, 1.0)), th = atan(view.y, view.x);\n"
    "  float R = 0.5 / u_scale;\n"
    "  vec3 c = popc(fract(a * 1.6 - u_t * 0.15)) * 0.55;\n"           /* rings pulsing outwards */
    "  float ray = 0.5 + 0.5 * sin(th * 12.0 + sin(a * 7.0 - u_t * 1.5) * 0.8 + u_t * 0.2);\n"
    "  c = mix(c, vec3(1.0, 0.65, 0.10), smoothstep(0.45, 0.55, ray) * smoothstep(R + 0.9, R, a) * 0.9);\n"
    "  c += vec3(1.0, 0.70, 0.20) * exp(-max(a - R, 0.0) * 5.0) * 0.4;\n"
    "  vec2 p = vec2(cos(th), sin(th)) * a / R;\n"
    "  vec2 e = eyes(0.3);\n"
    "  float grin = smoothstep(0.3, 0.9, sin(u_t * 0.3));\n"
    "  vec4 s = smiley(p, e.y, e.y, grin, vec3(1.0, 0.80, 0.15));\n"
    "  float cheek = smoothstep(0.16, 0.08, length(vec2(abs(p.x) - 0.6, p.y + 0.02)));\n"
    "  s.rgb = mix(s.rgb, vec3(1.0, 0.45, 0.35), cheek * 0.6);\n"
    "  c = mix(c, s.rgb, s.a);\n"
    "  return vec4(c * u_bright, 1.0);\n"
    "}\n";

/* ---- 5: peace and petals ---- */
static const char PEACE[] =
    "float seg(vec2 p, vec2 a, vec2 b){\n"
    "  vec2 pa = p - a, ba = b - a;\n"
    "  return length(pa - ba * clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0));\n"
    "}\n"
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  vec2 id; vec2 q = cells(dir, floor(6.0 * u_scale + 0.5), 0.0, id);\n"
    "  float s = sin(u_t * 0.4 + (id.x + id.y) * 0.35);\n"
    "  float sx = max(abs(s), 0.04);\n"                                /* the tile flipping over */
    "  float ck = mod(id.x + id.y, 2.0);\n"
    "  vec3 tile = ck < 0.5 ? vec3(1.0, 0.50, 0.05) : vec3(0.45, 0.10, 0.65);\n"
    "  vec3 ink  = ck < 0.5 ? vec3(0.45, 0.10, 0.65) : vec3(1.0, 0.85, 0.20);\n"
    "  if (u_theme > 0.5) { tile = popc(ck * 0.5 + 0.1); ink = popc(ck * 0.5 + 0.6); }\n"
    "  vec3 c = vec3(0.05);\n"
    "  if (abs(q.x) < 0.47 * sx && abs(q.y) < 0.47) {\n"
    "    c = tile * (0.55 + 0.45 * abs(s));\n"
    "    vec2 p = vec2(q.x / sx, q.y) / 0.42;\n"
    "    if (s > 0.0) {\n"
    "      float r = length(p);\n"
    "      float d = abs(r - 0.82);\n"
    "      d = min(d, seg(p, vec2(0.0, -0.82), vec2(0.0, 0.82)));\n"
    "      d = min(d, seg(p, vec2(0.0), vec2(-0.58, -0.58)));\n"
    "      d = min(d, seg(p, vec2(0.0), vec2(0.58, -0.58)));\n"
    "      c = mix(c, ink, smoothstep(0.13, 0.09, d));\n"
    "    } else {\n"
    "      vec4 f = flower(p, 8.0, 0.95, ink, tile * 0.6 + 0.4);\n"
    "      c = mix(c, f.rgb, f.a);\n"
    "    }\n"
    "  }\n"
    "  return vec4(c * u_bright, 1.0);\n"
    "}\n";

/* ---- 6: smiley rain (faces you) ---- */
static const char RAIN[] =
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  float x = atan(view.x, view.z), y = asin(clamp(view.y, -1.0, 1.0));\n"
    "  vec3 c = mix(vec3(0.55, 0.80, 1.0), vec3(1.0, 0.70, 0.85), 0.5 - 0.5 * view.y);\n"
    "  float ground = -0.45 + 0.04 * sin(x * 9.0);\n"
    "  if (y > ground) {\n"
    "    float sz = 0.32 / u_scale;\n"
    "    float col = floor(x / sz);\n"
    "    float sp = 0.5 + 0.8 * h3(vec3(col, 7.0, 7.0));\n"
    "    vec2 g = vec2(x / sz, y / sz + u_t * sp * 0.6 + h3(vec3(col, 8.0, 8.0)) * 9.0);\n"
    "    vec2 cell = floor(g), q = fract(g) - 0.5;\n"
    "    float h = h3(vec3(cell, 9.0)), h2 = h3(vec3(cell, 10.0));\n"
    "    if (h > 0.45) {\n"
    "      vec2 p = rot(q, u_t * (h2 - 0.5) * 3.0) / 0.34;\n"
    "      vec4 o;\n"
    "      if (h2 > 0.5) { vec2 e = eyes(h); o = smiley(p, e.x, 0.0, step(0.75, h2), vec3(1.0, 0.85, 0.1)); }\n"
    "      else o = flower(p, 5.0 + floor(h * 4.0), 1.0, popc(h), vec3(1.0, 0.85, 0.2));\n"
    "      c = mix(c, o.rgb, o.a);\n"
    "    }\n"
    "  } else {\n"
    /* the meadow, dotted with the ones that have landed */
    "    c = vec3(0.20, 0.60, 0.15) * (0.7 + 0.3 * sin(x * 60.0 + y * 20.0));\n"
    "    vec2 g = vec2(x, y) / (0.12 / u_scale);\n"
    "    vec2 cell = floor(g), q = fract(g) - 0.5;\n"
    "    float h = h3(vec3(cell, 11.0));\n"
    "    if (h > 0.4) {\n"
    "      vec4 o = flower(q / 0.4, 5.0, 1.0, popc(h * 3.0), vec3(1.0, 0.85, 0.2));\n"
    "      c = mix(c, o.rgb, o.a);\n"
    "    }\n"
    "  }\n"
    "  return vec4(c * u_bright, 1.0);\n"
    "}\n";

/* ---- 7: flower kaleidoscope ---- */
static const char KALEIDO[] =
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  float r = acos(clamp(abs(dir.y), 0.0, 1.0));\n"                 /* 0 at the poles */
    "  float th = atan(dir.x, dir.z);\n"
    "  float w = 2.0 * PI / 12.0;\n"
    "  float a = mod(th, w); a = min(a, w - a) / (0.5 * w);\n"          /* 0 at a mirror, 1 mid-wedge */
    "  float z = r * 4.0 * u_scale - u_t * 0.3;\n"
    "  float ring = floor(z), f = fract(z);\n"
    "  float open = 0.6 + 0.4 * sin(u_t * 0.8 + ring * 1.3);\n"
    "  float edge = 0.15 + 0.75 * pow(a, 0.8) * open;\n"
    "  vec3 c = mix(popc(ring / 6.0 + 0.5) * 0.45, popc(ring / 6.0) * (0.75 + 0.25 * a),\n"
    "               smoothstep(edge + 0.04, edge - 0.04, f));\n"
    "  c *= 1.0 - 0.6 * smoothstep(0.04, 0.0, abs(f - edge));\n"
    "  c = mix(c, vec3(1.0, 0.9, 0.4), smoothstep(0.15, 0.0, r));\n"
    "  return vec4(c * u_bright, 1.0);\n"
    "}\n";

/* ---- 8: rainbow face swirl (faces you) ---- */
static const char RAINBOW[] =
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  float a = acos(clamp(view.z, -1.0, 1.0)), th = atan(view.y, view.x);\n"
    "  float R = 0.42 / u_scale;\n"
    "  float s = th / (2.0 * PI) * 3.0 + a * 4.0 + 0.5 / (a + 0.3) - u_t * 0.5;\n"
    "  vec3 c = popc(fract(s));\n"
    "  float e6 = fract(s * 6.0);\n"
    "  c *= smoothstep(0.0, 0.08, min(e6, 1.0 - e6));\n"
    "  vec2 p = vec2(cos(th), sin(th)) * a / R;\n"
    "  vec2 e = eyes(0.7);\n"
    "  float grin = 0.5 + 0.5 * sin(u_t * 0.5);\n"
    "  vec4 f = smiley(p, e.y, e.y, grin, vec3(1.0, 0.85, 0.1));\n"
    "  c = mix(c * (1.0 - 0.5 * smoothstep(1.3, 1.0, length(p))), f.rgb, f.a);\n"  /* soft halo */
    "  return vec4(c * u_bright, 1.0);\n"
    "}\n";

/* ---- 9: hippie bus panel ---- */
static const char BUS[] =
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  float lat = asin(clamp(dir.y, -1.0, 1.0)), lon = atan(dir.x, dir.z);\n"
    "  float y0 = 0.05 + 0.06 * sin(lon * 3.0);\n"
    "  vec3 lower = u_theme > 0.5 ? popc(0.6) * 0.8 : vec3(0.10, 0.58, 0.60);\n"
    "  vec3 c = lat > y0 ? vec3(0.96, 0.90, 0.75) : lower;\n"
    "  float sw = 0.05 * sin(lon * 5.0 + u_t * 0.3);\n"
    "  c = mix(c, popc(0.2), smoothstep(0.035, 0.025, abs(lat - y0 - 0.12 - sw)));\n"
    "  c = mix(c, popc(0.05), smoothstep(0.035, 0.025, abs(lat - y0 - 0.20 - sw * 1.4)));\n"
    "  c = mix(c, vec3(0.35, 0.18, 0.08), smoothstep(0.03, 0.02, abs(lat - y0 + 0.08 + sw)));\n"
    "  vec2 id; vec2 q = cells(dir, floor(3.0 * u_scale + 0.5), 0.0, id);\n"
    "  float h = h3(vec3(id, 12.0)), h2 = h3(vec3(id, 13.0));\n"
    "  vec2 p = (q - (vec2(h, h2) - 0.5) * 0.15) / 0.36;\n"
    "  if (h > 0.88) {\n"
    "    vec2 e = eyes(h2);\n"
    "    vec4 s = smiley(p, e.x, 0.0, 0.0, vec3(1.0, 0.85, 0.1));\n"
    "    c = mix(c, s.rgb, s.a);\n"
    "  } else if (h > 0.45) {\n"
    "    vec4 f = flower(rot(p, h * 6.2832 + u_t * 0.1), 6.0, 1.0, popc(h2), vec3(1.0, 0.95, 0.85));\n"
    "    c = mix(c, f.rgb, f.a);\n"
    "  }\n"
    "  return vec4(c * u_bright, 1.0);\n"
    "}\n";

static const char *GLSL[NSTYLE] = {
    SMILEYS, DAISIES, WALLPAPER, GARDEN, SUNSHINE, PEACE, RAIN, KALEIDO, RAINBOW, BUS
};
static GLuint g_prog[NSTYLE];
static float  g_t;

static void init(void)
{
    for (int i = 0; i < NSTYLE; i++) {
        size_t n = strlen(LIB) + strlen(GLSL[i]) + 1;
        char *src = malloc(n);
        snprintf(src, n, "%s%s", LIB, GLSL[i]);
        g_prog[i] = dome_surface_program_themed(src);
        free(src);
    }
}

static int style(void)
{
    int s = (int)lroundf(L.style);
    return s < 0 ? 0 : s >= NSTYLE ? NSTYLE - 1 : s;
}

static void update(const DomeFrame *f) { g_t += f->dt * L.speed; }

static void draw(const DomeFrame *f)
{
    GLuint p = g_prog[style()];
    dome_surface_begin(p, f);
    glUniform1f(glGetUniformLocation(p, "u_t"), g_t);
    glUniform1f(glGetUniformLocation(p, "u_scale"), L.scale);
    glUniform1f(glGetUniformLocation(p, "u_bright"), L.bright);
    glUniform1f(glGetUniformLocation(p, "u_theme"), floorf(L.theme + 0.5f));
    glUniform1f(glGetUniformLocation(p, "u_wink"), L.wink);
    dome_surface_end();
}

static int key(SDL_Keycode k, int shift)
{
    (void)shift;
    if (k == SDLK_n) {
        L.style = (float)((style() + 1) % NSTYLE);
        printf("Groovy 2: %s\n", NAMES[style()]);
        return 1;
    }
    if (k == SDLK_m) {
        L.theme = (float)(((int)lroundf(L.theme) + 1) % (DOME_THEMES + 1));
        int t = (int)L.theme;
        printf("Groovy 2: %s colours\n", t == 0 ? "classic" : DOME_THEME_NAMES[t - 1]);
        return 1;
    }
    return 0;
}

Scene scene_groovy2 = {
    "GROOVY 2", "groovy2.cfg", 0, P, (int)(sizeof P / sizeof P[0]),
    init, NULL, update, draw, key,
    "N next style, M colours", "groovybaby.cfg"   /* its earlier name */
};
