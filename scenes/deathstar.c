/*
 * deathstar.c  -  the Death Star: a lit, panelled hull (surface) with the
 * green grid, superlaser dish and trench drawn over it as dots (points).
 * Set 'hull' to 0 for the original wireframe-only look, or 'lines' to 0
 * for a solid model with no grid.
 */
#include "../dome/dome.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define DISH_R 0.28f          /* chord radius of the dish opening */

static struct {
    float trail, dot_size, dot_sp, dish_dep, grid_deg, pole_dim, dish_lat;
    float hull, panels, lines;
} L = { 1.0f, 2.0f, 0.025f, 0.15f, 5.0f, 0.0f, -35.0f, 1.0f, 1.0f, 0.7f };

static Param P[] = {
    { "hull",     &L.hull,     0.05f,   0.0f, 1.5f, 0,
      "brightness of the solid hull (0 = wireframe only)" },
    { "panels",   &L.panels,   0.1f,    0.0f, 2.0f, 0,
      "contrast of the hull panelling" },
    { "lines",    &L.lines,    0.05f,   0.0f, 1.0f, 0,
      "brightness of the green grid lines (0 = none)" },
    { "trail",    &L.trail,    0.1f,    0.0f, 3.0f, 0,
      "motion trail strength (0 = off)" },
    { "dot_size", &L.dot_size, 0.25f,   1.0f, 8.0f, 0,
      "dot size in pixels" },
    { "dot_sp",   &L.dot_sp,   0.0025f, 0.005f, 0.06f, 1,
      "gap between dots along each line (smaller = more solid)" },
    { "dish_dep", &L.dish_dep, 0.01f,   0.0f, 0.60f, 1,
      "depth of the superlaser dish (fraction of the globe radius)" },
    { "grid_deg", &L.grid_deg, 0.5f,    2.0f, 30.0f, 1,
      "spacing of the surface grid in degrees" },
    { "pole_dim", &L.pole_dim, 0.1f,    0.0f, 1.0f, 1,
      "dim the lines towards the poles: 0 = even, 1 = classic banding" },
    { "dish_lat", &L.dish_lat, 5.0f,  -80.0f, 80.0f, 1,
      "dish position: negative = bottom half, positive = top half" },
};

static DomePoints g_pts;
static GLuint     g_prog;
static float      g_dish[3];
static float      g_dish_cos;

/* ---------------------------------------------------------------- */
/* Solid hull                                                        */
/* ---------------------------------------------------------------- */
static const char *HULL_GLSL =
    "uniform vec3  u_dish;\n"
    "uniform float u_dish_ang;\n"
    "uniform float u_dish_dep;\n"
    "uniform float u_hull;\n"
    "uniform float u_panels;\n"
    "float hash(vec2 p){ return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }\n"
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  vec3 light = normalize(vec3(-0.40, 0.50, 0.77));\n"
    "  float lat = asin(clamp(dir.y, -1.0, 1.0));\n"
    "  float lon = atan(dir.x, dir.z);\n"
    /* hull plating, laid out like a real build: latitude bands of fixed
     * height, each split into a whole number of plates, so every seam is
     * a straight line. Every 4th band and every 6th plate is a heavier
     * structural seam. Plates differ only slightly in tone. */
    "  float BAND = PI / 60.0;\n"                       /* 3 degree bands */
    "  float bi = floor(lat / BAND);\n"
    "  float fy = fract(lat / BAND);\n"
    "  float bc = (bi + 0.5) * BAND;\n"                 /* band centre    */
    "  float np = max(4.0, floor(120.0 * cos(bc) + 0.5));\n"
    "  float u = (lon / (2.0 * PI) + 0.5) * np;\n"
    "  float pi_ = floor(u), fx = fract(u);\n"
    "  float h1 = hash(vec2(pi_, bi));\n"
    "  float sx = min(fx, 1.0 - fx), sy = min(fy, 1.0 - fy);\n"
    "  float seam = smoothstep(0.0, 0.05, sx) * smoothstep(0.0, 0.05, sy);\n"
    "  float major_y = step(mod(bi, 4.0), 0.5) * (1.0 - smoothstep(0.0, 0.10, fy))\n"
    "                + step(3.5, mod(bi, 4.0)) * (1.0 - smoothstep(0.0, 0.10, 1.0 - fy));\n"
    "  float major_x = step(mod(pi_, 6.0), 0.5) * (1.0 - smoothstep(0.0, 0.12, fx));\n"
    "  float major = max(major_y, major_x);\n"
    "  float base = 0.72 + u_panels * (h1 - 0.5) * 0.07;\n"
    "  if (h1 > 0.93) base -= 0.06 * u_panels;\n"       /* the odd darker plate */
    "  base *= mix(1.0, (0.80 + 0.20 * seam) * (1.0 - 0.25 * major), min(u_panels, 1.0));\n"
    "  vec3 col = vec3(0.62, 0.65, 0.70) * base;\n"
    "  vec3 n = dir;\n"
    "  vec3 glow = vec3(0.0);\n"
    /* equatorial trench */
    "  if (abs(lat) < 0.0384) col *= 0.30;\n"
    /* superlaser dish: a parabolic bowl, lit with its own normals */
    "  float c = dot(dir, u_dish);\n"
    "  float ang = acos(clamp(c, -1.0, 1.0));\n"
    "  if (ang < u_dish_ang) {\n"
    "    float t = ang / u_dish_ang;\n"
    "    vec3 out_dir = normalize(dir - u_dish * c + 1e-5);\n"
    "    n = normalize(u_dish - out_dir * (2.0 * u_dish_dep * t / sin(u_dish_ang)));\n"
    "    float ring = min(fract(t * 5.0), 1.0 - fract(t * 5.0));\n"
    "    col = vec3(0.50, 0.52, 0.56) * (0.80 + 0.10 * h1)\n"
    "          * mix(0.65, 1.0, smoothstep(0.0, 0.06, ring));\n"
    "    glow = vec3(0.2, 1.0, 0.35) * smoothstep(0.16, 0.0, t) * 1.2;\n"
    "  }\n"
    "  float diff = max(dot(u_rot * n, light), 0.0);\n"
    "  vec3 lit = col * (0.16 + 0.90 * diff);\n"
    "  return vec4(lit * u_hull + glow * step(0.001, u_hull), 1.0);\n"
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

#define LINE(a, b, c, sp) dome_points_line(&g_pts, a, b, c[0], c[1], c[2], c[3], sp)

static void rebuild(void)
{
    DomePoints *pp = &g_pts;
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

static void draw(const DomeFrame *f)
{
    if (L.hull > 0.0f) {
        dome_surface_begin(g_prog, f);
        glUniform3fv(glGetUniformLocation(g_prog, "u_dish"), 1, g_dish);
        glUniform1f(glGetUniformLocation(g_prog, "u_dish_ang"), asinf(DISH_R));
        glUniform1f(glGetUniformLocation(g_prog, "u_dish_dep"), L.dish_dep);
        glUniform1f(glGetUniformLocation(g_prog, "u_hull"), L.hull);
        glUniform1f(glGetUniformLocation(g_prog, "u_panels"), L.panels);
        dome_surface_end();
    }
    if (L.lines <= 0.0f) return;
    dome_points_aperture(g_dish, g_dish_cos);
    mat3 rot;
    if (L.trail > 0.0f) {
        /* one faint copy a few degrees behind: half the cost of the two
         * copies it used to draw, and hard to tell apart on the globe */
        dome_rot_at(f, f->spin + DEG2RAD(-4.0f), rot);
        dome_draw_points(&g_pts, rot, fminf(1.0f, f->fade * 0.15f * L.trail * L.lines), L.dot_size);
    }
    dome_draw_points(&g_pts, f->rot, f->fade * L.lines, L.dot_size);
}

static int key(SDL_Keycode k, int shift)
{
    (void)shift;
    switch (k) {
    case SDLK_n: case SDLK_m:
        L.dot_sp = fmaxf(0.005f, fminf(0.06f, L.dot_sp * (k == SDLK_n ? 0.75f : 1.33f)));
        printf("dot_sp = %.4f\n", L.dot_sp); break;
    case SDLK_i: case SDLK_k:
        L.dish_dep = fmaxf(0.0f, fminf(0.60f, L.dish_dep + (k == SDLK_k ? 0.01f : -0.01f)));
        printf("dish_dep = %.3f\n", L.dish_dep); break;
    default: return 0;
    }
    rebuild();
    return 1;
}

Scene scene_deathstar = {
    "DEATH STAR", "deathstar.cfg", SDLK_d, P, (int)(sizeof P / sizeof P[0]),
    init, rebuild, NULL, draw, key,
    "N/M denser/sparser dots, I/K dish shallower/deeper",
    "look.cfg"
};
