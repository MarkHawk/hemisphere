/*
 * deathstar.c  -  the Death Star: a lit, panelled hull (surface) with the
 * green grid, superlaser dish and trench drawn over it as dots (points).
 * Set 'hull' to 0 for the original wireframe-only look, or 'lines' to 0
 * for a solid model with no grid.
 *
 * Intro: each time the scene comes up the wireframe starts as a single dot
 * at the centre of the dome and zooms out until it fills the globe, then
 * the hull fades in. Z replays it. 'phosphor' picks the line colour:
 * 0 green, 1 orange, 2 blue, 3 white (C cycles).
 *
 * 'style' (the web remote's style menu): 0 wireframe only, 1 hull and
 * wireframe, 2 wireframe fades to hull after wire_out seconds, 3 hull only.
 */
#include "../dome/dome.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define DISH_R 0.28f          /* chord radius of the dish opening */

/* Settings. Only the ones in P[] below show in the web remote and the
 * settings file; the rest are locked here at Mark's chosen look (21 Oct
 * 2026): dish in the top half at 30 degrees, large plates. */
static struct {
    /* shown */
    float style, phosphor, lines, dot_size, grid_deg, hull, detail, windows,
          intro, intro_s, wire_out;
    /* locked */
    float dish_lat, dish_dep, panels, panel_h, panel_w, seam_y, seam_x,
          pole_dim, trail, hull_in, wire_fade, start_lon;
    /* worked out: dot spacing that makes the dots touch */
    float dot_sp;
} L = {
    .style = 2.0f, .phosphor = 0.0f, .lines = 1.0f, .dot_size = 3.0f,
    .grid_deg = 10.0f, .hull = 1.2f, .detail = 1.0f, .windows = 1.0f,
    .intro = 1.0f, .intro_s = 8.0f, .wire_out = 11.0f,
    .dish_lat = 30.0f, .dish_dep = 0.10f, .panels = 1.9f, .panel_h = 5.0f,
    .panel_w = 10.0f, .seam_y = 3.0f, .seam_x = 4.0f, .pole_dim = 0.3f,
    .trail = 0.0f, .hull_in = 2.5f, .wire_fade = 3.0f, .start_lon = -55.0f,
    .dot_sp = 0.01f,
};

static Param P[] = {
    { "style",    &L.style,    1.0f,  0.0f, 3.0f, 0,
      "look: 0 wireframe only, 1 hull and wireframe, 2 wireframe fades to hull, 3 hull only" },
    { "phosphor", &L.phosphor, 1.0f,  0.0f, 3.0f, 1,
      "wireframe colour: 0 green, 1 orange, 2 blue, 3 white" },
    { "lines",    &L.lines,    0.05f, 0.0f, 1.0f, 0,
      "wireframe brightness" },
    { "dot_size", &L.dot_size, 0.25f, 1.0f, 8.0f, 1,
      "wireframe line thickness in pixels (the dots always join up)" },
    { "grid_deg", &L.grid_deg, 0.5f,  2.0f, 30.0f, 1,
      "spacing of the wireframe grid in degrees" },
    { "dish_dep", &L.dish_dep, 0.01f, 0.0f, 0.30f, 1,
      "depth of the superlaser dish (0.10 = like the film; I/K shallower/deeper)" },
    { "hull",     &L.hull,     0.05f, 0.0f, 1.5f, 0,
      "hull brightness" },
    { "detail",   &L.detail,   0.1f,  0.0f, 2.0f, 0,
      "small hull detail: sub-plates, raised blocks, grooves (0 = plain plates)" },
    { "windows",  &L.windows,  0.1f,  0.0f, 2.0f, 0,
      "brightness of the small lit windows, seen best on the dark side" },
    { "intro",    &L.intro,    1.0f,  0.0f, 1.0f, 0,
      "zoom out from a dot when the scene starts (1 = on, Z replays)" },
    { "intro_s",  &L.intro_s,  0.5f,  1.0f, 30.0f, 0,
      "seconds for the zoom out" },
    { "wire_out", &L.wire_out, 0.5f,  0.0f, 120.0f, 0,
      "style 2: seconds after the start when the wireframe fades away" },
};

static DomePoints g_pts;
static GLuint     g_prog;
static float      g_dish[3];
static float      g_dish_cos;
static float      g_intro_t0, g_last_now = -100.0f, g_prev_intro = -1.0f;
static int        g_replay;
static int        g_warm;              /* hull shader drawn once yet? */
static float      g_spin_off;          /* extra turn so the intro starts dish-left */

/* the extra spin that puts the dish start_lon degrees round from the front
 * (negative = to the left, as you face the globe). Done once per intro by
 * trying every degree: cheap and copes with any tilt or pitch. */
static float dish_offset(const DomeFrame *f)
{
    float want = DEG2RAD(L.start_lon), tx = sinf(want), tz = cosf(want);
    float best = -1e9f, best_a = 0.0f;
    for (int i = 0; i < 360; i++) {
        float a = DEG2RAD((float)i);
        mat3 r;
        dome_rot_at(f, f->spin + a, r);
        float dx = r[0] * g_dish[0] + r[3] * g_dish[1] + r[6] * g_dish[2];
        float dz = r[2] * g_dish[0] + r[5] * g_dish[1] + r[8] * g_dish[2];
        float sc = dx * tx + dz * tz;
        if (sc > best) { best = sc; best_a = a; }
    }
    return best_a;
}

/* phosphor colours; green (0) keeps the original palette untouched */
static const float PHOS[4][3] = {
    { 0.00f, 1.00f, 0.2549f },     /* green  */
    { 1.00f, 0.48f, 0.05f   },     /* orange */
    { 0.22f, 0.55f, 1.00f   },     /* blue   */
    { 0.92f, 0.95f, 1.00f   },     /* white  */
};
static int phos_i(void)
{
    int i = (int)lroundf(L.phosphor);
    return i < 0 ? 0 : i > 3 ? 3 : i;
}

/* ---------------------------------------------------------------- */
/* Solid hull                                                        */
/* ---------------------------------------------------------------- */
static const char *HULL_GLSL =
    "uniform vec3  u_dish;\n"
    "uniform float u_dish_ang;\n"
    "uniform float u_dish_dep;\n"
    "uniform float u_hull;\n"
    "uniform float u_panels;\n"
    "uniform float u_panel_h, u_panel_w, u_seam_y, u_seam_x;\n"
    "uniform vec3  u_glow;\n"
    "uniform float u_detail, u_windows;\n"
    "float hash(vec2 p){ return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }\n"
    "uniform float u_eye;\n"
    /* 1 inside the hull: below the bowl's floor, or under the rim */
    "float bowl_in(vec3 X){\n"
    "  float r = length(X);\n"
    "  float th = acos(clamp(dot(X / r, u_dish), -1.0, 1.0));\n"
    "  if (th >= u_dish_ang) return step(r, 1.0);\n"
    "  float q = th / u_dish_ang;\n"
    "  return step(r, 1.0 - u_dish_dep * (1.0 - q * q));\n"
    "}\n"
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  vec3 light = normalize(vec3(-0.40, 0.50, 0.77));\n"
    "  float lat = asin(clamp(dir.y, -1.0, 1.0));\n"
    "  float lon = atan(dir.x, dir.z);\n"
    /* hull plating, laid out like a real build: latitude bands of fixed
     * height (panel_h degrees), each split into a whole number of plates
     * about panel_w degrees wide, so every seam is a straight line. Every
     * seam_y-th band and seam_x-th plate is a heavier structural seam
     * (0 turns that off). Plates differ only slightly in tone. */
    "  float BAND = u_panel_h * PI / 180.0;\n"
    "  float bi = floor(lat / BAND);\n"
    "  float fy = fract(lat / BAND);\n"
    "  float bc = (bi + 0.5) * BAND;\n"                 /* band centre    */
    "  float np = max(4.0, floor((360.0 / u_panel_w) * cos(bc) + 0.5));\n"
    "  float u = (lon / (2.0 * PI) + 0.5) * np;\n"
    "  float pi_ = floor(u), fx = fract(u);\n"
    "  float h1 = hash(vec2(pi_, bi));\n"
    "  float sx = min(fx, 1.0 - fx), sy = min(fy, 1.0 - fy);\n"
    "  float seam = smoothstep(0.0, 0.05, sx) * smoothstep(0.0, 0.05, sy);\n"
    "  float sy_n = max(u_seam_y, 1.0), sx_n = max(u_seam_x, 1.0);\n"
    "  float major_y = step(0.5, u_seam_y) * (\n"
    "        step(mod(bi, sy_n), 0.5) * (1.0 - smoothstep(0.0, 0.10, fy))\n"
    "      + step(sy_n - 0.5, mod(bi, sy_n)) * (1.0 - smoothstep(0.0, 0.10, 1.0 - fy)));\n"
    "  float major_x = step(0.5, u_seam_x)\n"
    "        * step(mod(pi_, sx_n), 0.5) * (1.0 - smoothstep(0.0, 0.12, fx));\n"
    "  float major = max(major_y, major_x);\n"
    "  float base = 0.72 + u_panels * (h1 - 0.5) * 0.07;\n"
    "  if (h1 > 0.93) base -= 0.06 * u_panels;\n"       /* the odd darker plate */
    "  base *= mix(1.0, (0.80 + 0.20 * seam) * (1.0 - 0.25 * major), min(u_panels, 1.0));\n"
    /* finer detail inside each plate: 3 x 2 sub-plates with their own tone
     * and fine seams, some carrying a raised block (bevelled, lit from the
     * upper left) and some a groove across. All from hashes: no loops. */
    "  vec2 sc = vec2(fx * 3.0, fy * 2.0);\n"
    "  vec2 si = floor(sc), sf = fract(sc);\n"
    "  vec2 sid = vec2(pi_ * 3.0 + si.x, bi * 2.0 + si.y);\n"
    "  float h2 = hash(sid), h3 = hash(sid + 17.3);\n"
    "  float s2 = smoothstep(0.0, 0.07, min(sf.x, 1.0 - sf.x)) * smoothstep(0.0, 0.07, min(sf.y, 1.0 - sf.y));\n"
    "  float det = u_detail * (0.06 * (h2 - 0.5) - 0.09 * (1.0 - s2));\n"
    "  if (h2 > 0.72) {\n"                               /* raised block  */
    "    vec2 bq = (sf - 0.5) / (0.18 + 0.14 * h3);\n"
    "    if (max(abs(bq.x), abs(bq.y)) < 1.0) {\n"
    "      float edge = max(abs(bq.x), abs(bq.y));\n"
    "      float bev = step(0.72, edge) * (bq.y - bq.x > 0.0 ? 0.10 : -0.12);\n"
    "      det += u_detail * (0.07 + bev);\n"
    "    } else if (bq.x > 1.0 && bq.x < 1.5 && abs(bq.y) < 1.0) det -= u_detail * 0.07;\n"   /* its shadow */
    "  } else if (h2 < 0.12 && abs(sf.y - 0.5) < 0.10) det -= u_detail * 0.12;\n"          /* groove */
    "  base += det;\n"
    "  vec3 col = vec3(0.62, 0.65, 0.70) * base;\n"
    /* small lit windows: a few sub-plates carry a light, centred */
    "  float win = 0.0;\n"
    "  if (h3 > 0.80 && h2 <= 0.72) {\n"
    "    vec2 wq = abs(sf - 0.5);\n"
    "    win = step(wq.x, 0.22) * step(wq.y, 0.16) * (0.6 + 0.4 * fract(h3 * 7.0));\n"
    "  }\n"
    "  vec3 n = dir;\n"
    "  vec3 glow = vec3(0.0);\n"
    "  float dsh = 0.0, lip = 0.0;\n"
    /* equatorial trench */
    "  if (abs(lat) < 0.0384) {\n"
    "    float rib = fract(lon * 90.0 / PI);\n"           /* a rib every 2 degrees */
    "    col *= 0.30 * (0.75 + 0.25 * smoothstep(0.30, 0.45, abs(rib - 0.5)) * u_detail);\n"
    "    win = step(0.93, hash(vec2(floor(lon * 90.0 / PI), 3.0))) * step(abs(lat), 0.010)\n"
    "        * step(abs(rib - 0.5), 0.18);\n"
    "  }\n"
    /* superlaser dish: a real bowl. The line of sight from the viewer
     * (u_eye radii in front, as the wireframe uses) is traced into the
     * bowl, so the hull dish shifts with depth exactly like the wire rings.
     * Only dish pixels pay for the 16 + 5 steps. */
    "  float c = dot(dir, u_dish);\n"
    "  float ang = acos(clamp(c, -1.0, 1.0));\n"
    "  if (ang < u_dish_ang) {\n"
    "    vec3 E = u_rot_inv * vec3(0.0, 0.0, u_eye);\n"
    "    vec3 D = normalize(dir - E);\n"
    /* a glancing line of sight (dish near the edge of the globe) can cross
     * the whole bowl before it meets the far wall, so search that far */
    "    float gi = max(-dot(D, dir), 0.12);\n"
    "    float L = min(2.2 * sin(u_dish_ang) + u_dish_dep, (u_dish_dep + 0.04) / gi + 0.05);\n"
    "    float st = L / 16.0, s0 = L - st, s1 = L;\n"
    "    for (int i = 0; i < 16; i++) {\n"
    "      float s = st * float(i + 1);\n"
    "      if (bowl_in(dir + D * s) > 0.5) { s1 = s; s0 = s - st; break; }\n"
    "    }\n"
    "    for (int i = 0; i < 5; i++) {\n"
    "      float sm = 0.5 * (s0 + s1);\n"
    "      if (bowl_in(dir + D * sm) > 0.5) s1 = sm; else s0 = sm;\n"
    "    }\n"
    "    vec3 H = dir + D * s1;\n"
    "    vec3 Hn = normalize(H);\n"
    "    float hc = dot(Hn, u_dish);\n"
    "    float th = acos(clamp(hc, -1.0, 1.0));\n"
    "    float t = min(th / u_dish_ang, 1.0);\n"
    "    vec3 out_dir = normalize(Hn - u_dish * hc + 1e-5);\n"
    "    float wall = step(0.995, th / u_dish_ang);\n"      /* under the rim */
    "    n = normalize(Hn - out_dir * (1.6 * u_dish_dep * t / u_dish_ang));\n"
    "    if (wall > 0.5) n = normalize(-out_dir + Hn * 0.3);\n"
    /* shadow cast by the rim: step from the hit point towards the light by
     * its depth below the rim; if that leaves the bowl, the rim is in the
     * way. Works wherever the dish is, top or bottom. */
    "    vec3 lm = u_rot_inv * light;\n"
    "    float le = dot(lm, u_dish);\n"
    "    vec3 lt = lm - u_dish * le;\n"
    "    float drel = (1.0 - length(H)) / sin(u_dish_ang);\n"
    "    vec3 reach = out_dir * t + lt * (drel / max(le, 0.08));\n"
    "    dsh = max(smoothstep(0.96, 1.10, length(reach)) * 0.70, wall * 0.30);\n"
    "    lip = smoothstep(0.92, 0.99, ang / u_dish_ang) * max(dot(normalize(dir - u_dish * c + 1e-5), -normalize(lt + 1e-5)), 0.0) * (1.0 - wall) * 0.4;\n"
    /* plating inside the bowl, like the hull's but laid out in rings:
     * 15 bands out from the centre, each cut into plates round it */
    "    vec3 a1 = normalize(vec3(u_dish.z, 0.0, -u_dish.x) + 1e-5);\n"
    "    vec3 a2 = cross(u_dish, a1);\n"
    "    float az = atan(dot(out_dir, a2), dot(out_dir, a1)) / (2.0 * PI) + 0.5;\n"
    "    float bt = t * 15.0, bk = floor(bt), bf = fract(bt);\n"
    "    float nseg = max(6.0, floor(9.0 * (bk + 0.5)));\n"
    "    float sg = az * nseg, sk = floor(sg), sfr = fract(sg);\n"
    "    float hd = hash(vec2(sk, bk + 40.0));\n"
    "    float dseam = smoothstep(0.0, 0.08, min(bf, 1.0 - bf)) * smoothstep(0.0, 0.08, min(sfr, 1.0 - sfr));\n"
    "    float dtone = 0.70 + 0.07 * (hd - 0.5) * u_panels - 0.04 * step(0.9, hd) * u_detail;\n"
    "    dtone *= mix(1.0, 0.82 + 0.18 * dseam, min(u_panels, 1.0));\n"
    "    float ring = min(fract(t * 5.0), 1.0 - fract(t * 5.0));\n"
    "    col = vec3(0.60, 0.62, 0.67) * dtone\n"
    "          * mix(0.60, 1.0, smoothstep(0.0, 0.05, ring)) * (1.0 - 0.20 * wall);\n"
    "    glow = u_glow * smoothstep(0.16, 0.0, t) * 1.2 * (1.0 - wall);\n"
    "    win = 0.0;\n"
    "  }\n"
    "  float diff = max(dot(u_rot * n, light), 0.0) * (1.0 - dsh);\n"
    /* a little extra fill light inside the dish so its shadowed side
     * stays readable rather than going black */
    "  vec3 lit = col * (0.16 + 0.10 * step(0.0001, dsh + lip + float(ang < u_dish_ang)) + 0.90 * diff + 0.35 * lip);\n"
    "  vec3 wl = vec3(1.0, 0.88, 0.62) * win * u_windows * (0.60 - 0.52 * diff);\n"
    "  return vec4((lit + wl) * u_hull + glow * step(0.001, u_hull), 1.0);\n"
    "}\n";

/* ---------------------------------------------------------------- */
/* Dots                                                              */
/* ---------------------------------------------------------------- */
static void col_lat(float ld, float c[4])
{
    float al = fabsf(ld);
    if (al <= 5.0f) { c[0] = 0; c[1] = 1.0f; c[2] = 0.2549f; c[3] = 1.0f; return; }
    float bg, bb, ba;
    if (al <= 45.0f) { bg = 0.8f; bb = 0.2f;   ba = 0.70f; }
    else             { bg = 0.4f; bb = 0.133f; ba = 0.47f; }
    float k = fmaxf(0.0f, fminf(1.0f, L.pole_dim));
    c[0] = 0;
    c[1] = 0.85f + (bg - 0.85f) * k;
    c[2] = 0.22f + (bb - 0.22f) * k;
    c[3] = 0.80f + (ba - 0.80f) * k;
}

/* The palette is written in green (r = 0, g = brightness) plus white
 * (r = 1). Other phosphors keep the brightness and swap the hue; white
 * parts become a pale, hot version of the phosphor. */
static void line_t(const float a[3], const float b[3], const float c[4], float sp)
{
    int i = phos_i();
    if (i == 0) { dome_points_line(&g_pts, a, b, c[0], c[1], c[2], c[3], sp); return; }
    const float *ph = PHOS[i];
    float w = c[0], lv = c[1], o[3];
    for (int q = 0; q < 3; q++)
        o[q] = ph[q] * lv * (1.0f - w) + (ph[q] + (1.0f - ph[q]) * 0.55f) * w;
    dome_points_line(&g_pts, a, b, o[0], o[1], o[2], c[3], sp);
}
#define LINE(a, b, c, sp) line_t(a, b, c, sp)

/* pixels per radian near the dome centre, from the screen size (set in
 * draw); used to space the dots so neighbours overlap */
static float g_pxr = 270.0f;

static void rebuild(void)
{
    DomePoints *pp = &g_pts;
    L.dot_sp = fmaxf(0.004f, fminf(0.06f, 0.8f * L.dot_size / g_pxr));
    dome_points_clear(pp);
    float a[3], b[3], c[4];

    dome_sph(DEG2RAD(fmaxf(-80.0f, fminf(80.0f, L.dish_lat))), 0.0f, g_dish);
    g_dish_cos = cosf(asinf(DISH_R));
    dome_points_mask(pp, g_dish, g_dish_cos);    /* hole in the hull grid */

    float gd = fmaxf(2.0f, fminf(30.0f, L.grid_deg));
    int NLAT = (int)lroundf(180.0f / gd), NLON = (int)lroundf(360.0f / gd);
    if (NLAT < 2) NLAT = 2;
    if (NLON < 4) NLON = 4;
    NLON = (NLON + 3) & ~3;
    const float LATSTEP = 180.0f / NLAT, LAT0 = -90.0f + 0.5f * LATSTEP;
    const float sp = fminf(L.dot_sp, DEG2RAD(fminf(LATSTEP, 360.0f / NLON)) / 8.0f);

    for (int li = 0; li < NLAT; li++) {
        float latd = LAT0 + li * LATSTEP, lat = DEG2RAD(latd);
        /* the trench has its own wall lines at +-2.2 degrees: a grid line
         * close beside them crowds the trench, so leave it out */
        if (fabsf(latd) < 2.2f + 0.4f * LATSTEP) continue;
        col_lat(latd, c);
        for (int lo = 0; lo < NLON; lo++) {
            dome_sph(lat, DEG2RAD(lo * 360.0f / NLON), a);
            dome_sph(lat, DEG2RAD((lo + 1) * 360.0f / NLON), b);
            LINE(a, b, c, sp);
        }
    }
    /* meridians, thinned towards the poles so the caps don't glow */
    for (int lo = 0; lo < NLON; lo++) {
        float lon = DEG2RAD(lo * 360.0f / NLON);
        for (int li = 0; li < NLAT - 1; li++) {
            float md = LAT0 + (li + 0.5f) * LATSTEP;
            int every = fabsf(md) > 75.0f ? 4 : fabsf(md) > 60.0f ? 2 : 1;
            if (lo % every) continue;
            col_lat(md, c);
            dome_sph(DEG2RAD(LAT0 + li * LATSTEP), lon, a);
            dome_sph(DEG2RAD(LAT0 + (li + 1) * LATSTEP), lon, b);
            LINE(a, b, c, sp);
        }
    }

    /* superlaser dish: parabolic bowl set into the surface */
    dome_points_mask(pp, g_dish, 2.0f);
    float n[3] = { g_dish[0], g_dish[1], g_dish[2] };
    float t1[3] = { n[2], 0.0f, -n[0] };
    float il = 1.0f / sqrtf(t1[0] * t1[0] + t1[2] * t1[2] + 1e-9f);
    t1[0] *= il; t1[2] *= il;
    float t2[3] = { n[1] * t1[2] - n[2] * t1[1], n[2] * t1[0] - n[0] * t1[2],
                    n[0] * t1[1] - n[1] * t1[0] };
    const int NCURVE = 5, DSEG = 48, NSPOKES = 12;
    float max_ins = fmaxf(0.0f, fminf(0.6f, L.dish_dep));
    float half_ang = asinf(DISH_R);

#define BOWL(out, t, ang) do { \
        float ra_ = half_ang * (t), ins_ = max_ins * (1.0f - (t) * (t)); \
        float s_[3]; \
        for (int q_ = 0; q_ < 3; q_++) \
            s_[q_] = n[q_] * cosf(ra_) + sinf(ra_) * (cosf(ang) * t1[q_] + sinf(ang) * t2[q_]); \
        float l_ = sqrtf(s_[0] * s_[0] + s_[1] * s_[1] + s_[2] * s_[2]) + 1e-9f; \
        for (int q_ = 0; q_ < 3; q_++) (out)[q_] = s_[q_] / l_ * (1.0f - ins_); \
    } while (0)

    for (int ri = 1; ri <= NCURVE; ri++) {
        float t = (float)ri / NCURVE;
        if (ri == NCURVE) { c[0] = 0; c[1] = 1.0f; c[2] = 0.2549f; c[3] = 1.0f;  }
        else if (ri >= 3) { c[0] = 0; c[1] = 0.8f; c[2] = 0.2f;    c[3] = 0.85f; }
        else              { c[0] = 1; c[1] = 1;    c[2] = 1;       c[3] = 0.9f;  }
        for (int i = 0; i < DSEG; i++) {
            BOWL(a, t, DEG2RAD(i * 360.0f / DSEG));
            BOWL(b, t, DEG2RAD((i + 1) * 360.0f / DSEG));
            LINE(a, b, c, L.dot_sp * 0.5f);
        }
    }
    for (int s = 0; s < NSPOKES; s++) {
        float ang = DEG2RAD(s * 360.0f / NSPOKES);
        for (int seg = 0; seg < NCURVE; seg++) {
            if (seg == NCURVE - 1) { c[0] = 0; c[1] = 1.0f; c[2] = 0.2549f; c[3] = 1.0f;  }
            else if (seg >= 2)     { c[0] = 0; c[1] = 0.8f; c[2] = 0.2f;    c[3] = 0.85f; }
            else                   { c[0] = 1; c[1] = 1;    c[2] = 1;       c[3] = 0.9f;  }
            BOWL(a, (float)(NCURVE - seg) / NCURVE, ang);
            BOWL(b, (float)(NCURVE - seg - 1) / NCURVE, ang);
            LINE(a, b, c, L.dot_sp * 0.5f);
        }
    }
    float white[4] = { 1, 1, 1, 1 };
    for (int i = 0; i < 4; i++) {
        float ang = DEG2RAD(i * 45.0f);
        for (int q = 0; q < 3; q++) {
            float f = n[q] * (1.0f - max_ins), o = 0.035f * (cosf(ang) * t1[q] + sinf(ang) * t2[q]);
            a[q] = f + o; b[q] = f - o;
        }
        LINE(a, b, white, L.dot_sp * 0.3f);
    }

    /* equatorial trench: wall rings, floor, ribs */
    float prim[4] = { 0, 1.0f, 0.2549f, 1.0f }, sec[4] = { 0, 0.8f, 0.2f, 0.7f },
          dim[4] = { 0, 0.4f, 0.133f, 0.47f };
    float tl[2] = { DEG2RAD(2.2f), DEG2RAD(-2.2f) };
    for (int ti = 0; ti < 2; ti++)
        for (int lo = 0; lo < 180; lo++) {
            dome_sph(tl[ti], DEG2RAD(lo * 2.0f), a);
            dome_sph(tl[ti], DEG2RAD((lo + 1) * 2.0f), b);
            LINE(a, b, prim, L.dot_sp * 0.6f);
        }
    for (int lo = 0; lo < 180; lo++) {
        dome_sph(0.0f, DEG2RAD(lo * 2.0f), a);
        dome_sph(0.0f, DEG2RAD((lo + 1) * 2.0f), b);
        for (int q = 0; q < 3; q += 2) { a[q] *= 0.9995f; b[q] *= 0.9995f; }
        LINE(a, b, sec, L.dot_sp * 0.6f);
    }
    for (int ri = 0; ri < 36; ri++) {
        dome_sph(tl[0], DEG2RAD(ri * 10.0f), a);
        dome_sph(tl[1], DEG2RAD(ri * 10.0f), b);
        LINE(a, b, dim, L.dot_sp * 0.5f);
    }
    dome_points_upload(pp);
}

/* ---------------------------------------------------------------- */
static void init(void)
{
    dome_points_init(&g_pts, 200000);
    g_prog = dome_surface_program(HULL_GLSL);
    rebuild();
}

/* apparent size of the globe during the intro, 1 when it is over */
static float intro_zoom(float t)
{
    if (t >= 1.0f) return 1.0f;
    if (t <= 0.0f) return 0.002f;
    float e = t * t * (3.0f - 2.0f * t);         /* ease in and out */
    return powf(0.002f, 1.0f - e);               /* grows like an approach */
}

static void draw(const DomeFrame *f0)
{
    /* the scene's own turn: the frame's spin plus the intro offset */
    float pxr = 0.5f * (float)(f0->width < f0->height ? f0->width : f0->height) / 2.0f;
    if (pxr > 50.0f && fabsf(pxr - g_pxr) > 0.1f * g_pxr) { g_pxr = pxr; rebuild(); }
    DomeFrame ff = *f0;
    const DomeFrame *f = &ff;
    ff.spin += g_spin_off;
    dome_rot_at(&ff, ff.spin, ff.rot);
    /* restart the intro when the scene comes up (a gap in frames means it
     * was off screen), when Z is pressed, or when 'intro' is switched on */
    /* a gap longer than this frame's own dt means another scene was on;
     * a merely slow frame (start-up, a rebuild) has a big dt too, so it
     * does not restart the zoom */
    if (f->now - g_last_now > f->dt + 0.25f || g_replay ||
        (L.intro > 0.5f && g_prev_intro <= 0.5f))
    {
        g_intro_t0 = f->now;
        if (L.intro > 0.5f) {
            g_spin_off = dish_offset(f0);
            ff.spin = f0->spin + g_spin_off;
            dome_rot_at(&ff, ff.spin, ff.rot);
        }
    }
    g_last_now = f->now; g_replay = 0; g_prev_intro = L.intro;

    /* the look picks what is drawn; hull and lines stay as brightnesses */
    int style = (int)lroundf(L.style);
    float hull_b  = style == 0 ? 0.0f : L.hull;
    float lines_b = style == 3 ? 0.0f : L.lines;
    float zoom = 1.0f, hull_k = 1.0f, wire_k = 1.0f;
    if (L.intro > 0.5f && lines_b <= 0.0f && hull_b > 0.0f) {
        /* hull only: no zoom, the hull fades in */
        float el = f->now - g_intro_t0;
        hull_k = L.hull_in > 0.01f ? fmaxf(0.0f, fminf(1.0f, el / L.hull_in)) : 1.0f;
    }
    if (L.intro > 0.5f && lines_b > 0.0f) {
        float el = f->now - g_intro_t0, dur = fmaxf(1.0f, L.intro_s);
        zoom = intro_zoom(el / dur);
        /* once the hull is showing, the wireframe can bow out */
        if (style == 2 && hull_b > 0.0f && L.wire_out > 0.0f)
            wire_k = 1.0f - fmaxf(0.0f, fminf(1.0f, (el - L.wire_out) / fmaxf(0.1f, L.wire_fade)));
        hull_k = L.hull_in > 0.01f ? fmaxf(0.0f, fminf(1.0f, (el - dur) / L.hull_in))
                                   : (el >= dur ? 1.0f : 0.0f);
    }

    /* The first time the hull shader is used the Pi's driver finishes
     * preparing it, which shows as a hitch. Draw it once, black, on the
     * very first frame (the screen is still only a dot then) so the hitch
     * never lands on the wire-to-hull fade. */
    int warm = !g_warm && hull_b > 0.0f;
    if ((hull_b > 0.0f && hull_k > 0.0f) || warm) {
        g_warm = 1;
        dome_surface_begin(g_prog, f);
        glUniform3fv(glGetUniformLocation(g_prog, "u_dish"), 1, g_dish);
        glUniform1f(glGetUniformLocation(g_prog, "u_dish_ang"), asinf(DISH_R));
        glUniform1f(glGetUniformLocation(g_prog, "u_dish_dep"), L.dish_dep);
        glUniform1f(glGetUniformLocation(g_prog, "u_hull"), hull_b * hull_k);
        {
            const float *ph = PHOS[phos_i()];
            float gl3[3] = { 0.2f, 1.0f, 0.35f };
            if (phos_i() != 0)
                for (int q = 0; q < 3; q++) gl3[q] = (ph[q] + (1.0f - ph[q]) * 0.2f) * hull_k;
            else
                for (int q = 0; q < 3; q++) gl3[q] *= hull_k;
            glUniform3fv(glGetUniformLocation(g_prog, "u_glow"), 1, gl3);
            glUniform1f(glGetUniformLocation(g_prog, "u_detail"), L.detail);
            glUniform1f(glGetUniformLocation(g_prog, "u_windows"), L.windows);
        }
        glUniform1f(glGetUniformLocation(g_prog, "u_panels"), L.panels);
        glUniform1f(glGetUniformLocation(g_prog, "u_panel_h"), L.panel_h);
        glUniform1f(glGetUniformLocation(g_prog, "u_panel_w"), L.panel_w);
        glUniform1f(glGetUniformLocation(g_prog, "u_seam_y"), floorf(L.seam_y + 0.5f));
        glUniform1f(glGetUniformLocation(g_prog, "u_seam_x"), floorf(L.seam_x + 0.5f));
        dome_surface_end();
    }
    if (lines_b <= 0.0f || wire_k <= 0.0f) return;
    dome_points_aperture(g_dish, g_dish_cos);
    dome_points_zoom(zoom);
    /* the tiny ball is a pile of dots: dim it */
    float za = (0.45f + 0.55f * zoom) * wire_k;
    if (L.intro > 0.5f)                 /* fade in from nothing as the zoom starts */
        za *= fminf(1.0f, fmaxf(0.0f, (f->now - g_intro_t0) / 0.8f));
    if (za <= 0.0f) { dome_points_zoom(1.0f); return; }
    mat3 rot;
    if (L.trail > 0.0f) {
        /* one faint copy a few degrees behind: half the cost of the two
         * copies it used to draw, and hard to tell apart on the globe */
        dome_rot_at(f, f->spin + DEG2RAD(-4.0f), rot);
        dome_draw_points(&g_pts, rot, fminf(1.0f, f->fade * 0.15f * L.trail * lines_b) * za, L.dot_size);
    }
    dome_draw_points(&g_pts, f->rot, f->fade * lines_b * za, L.dot_size);
    dome_points_zoom(1.0f);
}

static int key(SDL_Keycode k, int shift)
{
    (void)shift;
    switch (k) {
    case SDLK_n: case SDLK_m:
        L.dot_size = fmaxf(1.0f, fminf(8.0f, L.dot_size + (k == SDLK_m ? 0.25f : -0.25f)));
        printf("dot_size = %.2f\n", L.dot_size); break;
    case SDLK_i: case SDLK_k:
        L.dish_dep = fmaxf(0.0f, fminf(0.30f, L.dish_dep + (k == SDLK_k ? 0.01f : -0.01f)));
        printf("dish_dep = %.2f\n", L.dish_dep); break;
    case SDLK_z:
        g_replay = 1;
        printf("Intro replay\n"); return 1;
    case SDLK_c: {
        static const char *nm[4] = { "green", "orange", "blue", "white" };
        L.phosphor = (float)((phos_i() + 1) % 4);
        printf("phosphor = %d (%s)\n", phos_i(), nm[phos_i()]); break;
    }
    default: return 0;
    }
    rebuild();
    return 1;
}

Scene scene_deathstar = {
    "DEATH STAR", "deathstar.cfg", SDLK_d, P, (int)(sizeof P / sizeof P[0]),
    init, rebuild, NULL, draw, key,
    "N/M thinner/thicker lines, I/K dish shallower/deeper, C phosphor colour, Z replay intro",
    "look.cfg"
};
