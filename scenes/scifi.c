/*
 * scifi.c  -  science-fiction displays. N steps through the styles:
 *
 *   0 hologram globe   flickering wireframe planet, scanlines, rotating
 *                      targeting rings and readouts
 *   1 radar sweep      rotating sweep with fading contacts. If orbitfeed
 *                      is running, the contacts are the real satellites
 *                      passing over home (north up); otherwise made up
 *   2 hyperspace       stars stretch into streaks, then drop back to
 *                      normal space, over and over
 *   3 planet scan      an alien world with contour lines, swept by a
 *                      scanning beam
 *   4 reactor core     a pulsing energy ball with crawling electric arcs
 *   5 ship's computer  concentric rings of ticks, gauges and numbers
 *                      turning at different speeds
 *
 * All original designs, not copies of any film or show's screens.
 */
#include "../dome/dome.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define NSTYLE 6
static const char *NAMES[NSTYLE] = {
    "hologram globe", "radar sweep", "hyperspace", "planet scan", "reactor core", "ship's computer"
};

static struct { float style, speed, scale, range, home_lat, home_lon, text, bright; } L =
    { 0.0f, 1.0f, 1.0f, 2500.0f, 51.44f, 0.15f, 1.0f, 1.0f };

static Param P[] = {
    { "style",    &L.style,    1.0f,  0.0f, 5.0f, 0,
      "0 hologram, 1 radar, 2 hyperspace, 3 planet scan, 4 reactor, 5 computer (N)" },
    { "speed",    &L.speed,    0.1f,  0.0f, 4.0f, 0, "speed of the animation" },
    { "scale",    &L.scale,    0.05f, 0.4f, 2.5f, 0, "size of the pattern (bigger = finer)" },
    { "range",    &L.range,  100.0f, 300.0f, 8000.0f, 0, "radar range in km" },
    { "home_lat", &L.home_lat, 0.5f, -90.0f, 90.0f, 0, "radar centre latitude" },
    { "home_lon", &L.home_lon, 0.5f, -180.0f, 180.0f, 0, "radar centre longitude" },
    { "text",     &L.text,     1.0f,  0.0f, 1.0f, 0, "readouts: 0 off, 1 on" },
    { "bright",   &L.bright,   0.05f, 0.2f, 2.0f, 0, "brightness" },
};

#define U "uniform float u_t, u_scale, u_bright, u_a, u_b;\n"

/* ---- 0: hologram globe ---- */
static const char HOLO[] =
    U
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  float lat = asin(clamp(dir.y, -1.0, 1.0)), lon = atan(dir.x, dir.z);\n"
    "  float gy = abs(fract(lat / PI * 12.0 + 0.5) - 0.5);\n"
    "  float gx = abs(fract(lon / PI * 12.0 + 0.5) - 0.5) * max(cos(lat), 0.15);\n"
    "  float grid = max(smoothstep(0.03, 0.0, gy), smoothstep(0.03, 0.0, gx));\n"
    "  float h = fbm(dir * 2.2 * u_scale);\n"
    "  float coast = smoothstep(0.022, 0.0, abs(h - 0.52));\n"
    "  float land = smoothstep(0.515, 0.53, h);\n"
    "  float I = grid * 0.40 + coast * 1.0 + land * 0.10;\n"
    "  I *= 0.72 + 0.28 * sin(view.y * 420.0 + u_t * 6.0);\n"           /* scanlines */
    "  float sy = 1.0 - fract(u_t * 0.12) * 2.2;\n"
    "  float band = exp(-pow((view.y - sy) * 7.0, 2.0));\n"
    "  I += band * (0.20 + coast * 0.8);\n"
    "  I *= 0.88 + 0.12 * n3(vec3(u_t * 14.0, 0.0, 0.0));\n"            /* flicker */
    "  float a = acos(clamp(view.z, -1.0, 1.0)), th = atan(view.y, view.x);\n"
    "  float r1 = smoothstep(0.008, 0.0, abs(a - 1.05)) * step(0.45, fract(th * 3.0 / PI + u_t * 0.15));\n"
    "  float r2 = smoothstep(0.006, 0.0, abs(a - 1.22)) * step(0.6, fract(th * 36.0 / 6.283 - u_t * 0.05));\n"
    "  float r3 = smoothstep(0.004, 0.0, abs(a - 1.15)) * step(0.85, fract(th * 2.0 / 6.283 + u_t * 0.3));\n"
    "  float xh = (smoothstep(0.004, 0.0, abs(view.x)) + smoothstep(0.004, 0.0, abs(view.y)))\n"
    "              * step(a, 0.10) * step(0.03, a);\n"
    "  vec3 cyan = vec3(0.20, 0.85, 1.0);\n"
    "  vec3 col = cyan * I + vec3(0.02, 0.05, 0.09);\n"
    "  col += cyan * (r1 * 0.9 + r2 * 0.7) + vec3(1.0, 0.6, 0.2) * r3 + cyan * xh;\n"
    "  return vec4(col * u_bright, 1.0);\n"
    "}\n";

/* ---- 1: radar sweep (u_a = sweep angle) ---- */
static const char RADAR[] =
    U
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  float a = acos(clamp(view.z, -1.0, 1.0)), th = atan(view.y, view.x);\n"
    "  float R = 1.25;\n"
    "  if (a > R + 0.02) return vec4(0.0, 0.0, 0.0, 1.0);\n"
    "  float ring = smoothstep(0.005, 0.0, abs(fract(a / (R / 4.0) + 0.5) - 0.5) * (R / 4.0));\n"
    "  float sp = abs(fract(th / (PI / 6.0) + 0.5) - 0.5) * (PI / 6.0) * a;\n"
    "  float spoke = smoothstep(0.004, 0.0, sp);\n"
    "  float rim = smoothstep(0.012, 0.0, abs(a - R));\n"
    "  float d = mod(u_a - th, 2.0 * PI);\n"                     /* behind the beam */
    "  float trail = exp(-d * 2.2) * step(a, R);\n"
    "  float beam = smoothstep(0.03, 0.0, d * max(a, 0.05)) * step(a, R);\n"
    "  vec3 g = vec3(0.25, 1.0, 0.45);\n"
    "  vec3 col = vec3(0.0, 0.06, 0.02) + g * (ring * 0.30 + spoke * 0.18 + rim * 0.7 + trail * 0.30 + beam * 0.9);\n"
    "  col *= 0.93 + 0.07 * n3(view * 90.0 + u_t * 3.0);\n"
    "  return vec4(col * u_bright, 1.0);\n"
    "}\n";

/* ---- 2: hyperspace (u_a = warp 0..1, u_b = flash) ---- */
static const char WARP[] =
    U
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  float a = acos(clamp(view.z, -1.0, 1.0)), th = atan(view.y, view.x);\n"
    "  vec3 col = vec3(0.0);\n"
    "  for (int l = 0; l < 3; l++) {\n"
    "    float fl = float(l);\n"
    "    float N = 80.0 * (fl + 1.0) * u_scale;\n"
    "    float cell = floor((th / (2.0 * PI) + 0.5) * N);\n"
    "    float cth = (cell + 0.5) / N * 2.0 * PI - PI;\n"
    "    float h = h3(vec3(cell, fl, 5.0));\n"
    "    if (h < 0.35) continue;\n"
    "    float s = fract(h * 13.1 - u_t * (0.25 + 0.08 * fl));\n"     /* distance to the star */
    "    float rs = min(0.12 / (s + 0.08), 1.6);\n"
    "    float tail = rs * (1.0 - 0.75 * u_a) - 0.012;\n"
    "    float dth = abs(mod(th - cth + PI, 2.0 * PI) - PI) * max(a, 0.02);\n"
    "    float w = 0.0018 + 0.0035 * (1.0 - s);\n"
    "    float line = smoothstep(w, 0.0, dth) * smoothstep(tail - 0.01, tail, a) * smoothstep(rs + 0.01, rs, a);\n"
    "    col += mix(vec3(0.8, 0.85, 1.0), vec3(0.5, 0.7, 1.0), fl / 2.0) * line * (0.5 + 0.8 * (1.0 - s));\n"
    "  }\n"
    "  col += vec3(0.30, 0.50, 1.0) * exp(-a * 5.0) * (0.15 + 0.85 * u_a);\n"
    "  col += vec3(0.8, 0.9, 1.0) * u_b;\n"
    "  return vec4(col * u_bright, 1.0);\n"
    "}\n";

/* ---- 3: planet scan ---- */
static const char SCAN[] =
    U
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  float h = fbm(dir * 2.0 * u_scale + 3.0);\n"
    "  vec3 base = mix(vec3(0.10, 0.03, 0.18), vec3(0.06, 0.32, 0.32), smoothstep(0.35, 0.65, h));\n"
    "  base = mix(base, vec3(0.55, 0.30, 0.12), smoothstep(0.62, 0.72, h));\n"
    "  float line = smoothstep(0.43, 0.49, abs(fract(h * 14.0) - 0.5));\n"
    "  float sy = 1.1 - mod(u_t * 0.12, 2.6);\n"
    "  float done = smoothstep(0.0, 0.02, view.y - sy) * exp(-max(view.y - sy, 0.0) * 1.2);\n"
    "  float beam = exp(-pow((view.y - sy) * 50.0, 2.0));\n"
    "  float lat = asin(clamp(dir.y, -1.0, 1.0)), lon = atan(dir.x, dir.z);\n"
    "  float grid = max(smoothstep(0.02, 0.0, abs(fract(lat / PI * 9.0 + 0.5) - 0.5)),\n"
    "                   smoothstep(0.02, 0.0, abs(fract(lon / PI * 9.0 + 0.5) - 0.5) * max(cos(lat), 0.2)));\n"
    "  vec3 col = base * 0.65 + vec3(0.3, 1.0, 0.9) * line * (0.12 + 0.88 * done);\n"
    "  col += vec3(0.3, 1.0, 0.9) * grid * 0.08 + vec3(0.6, 1.0, 1.0) * beam * 0.8;\n"
    "  return vec4(col * u_bright, 1.0);\n"
    "}\n";

/* ---- 4: reactor core. Built from sine waves rather than noise, so it is
 * cheap enough for the Pi 3: a few sums per pixel. ---- */
static const char CORE[] =
    U
    "float waves(vec3 p, float t){\n"
    "  return sin(p.x * 3.1 + t) + sin(p.y * 3.7 - t * 1.3) + sin(p.z * 2.9 + t * 0.7)\n"
    "       + sin((p.x + p.y) * 4.3 - t * 0.9) + sin((p.y - p.z) * 5.1 + t * 1.7);\n"
    "}\n"
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  float t = u_t;\n"
    "  vec3 p = dir * 2.0 * u_scale;\n"
    /* plasma: two layers of waves, the second warped by the first */
    "  float w1 = waves(p, t * 0.6);\n"
    "  float w2 = waves(p * 2.3 + w1 * 0.35, -t * 0.9);\n"
    "  float n = 0.5 + 0.11 * w1 + 0.05 * w2;\n"
    /* bright bands rolling from pole to pole */
    "  float band = smoothstep(0.85, 1.0, sin(dir.y * 14.0 - t * 3.0));\n"
    /* electric arcs: thin lines where the wave fields cross zero */
    "  float flick = step(0.35, fract(sin(floor(t * 9.0) * 12.9898) * 43758.5453));\n"
    "  float arc1 = smoothstep(0.16, 0.0, abs(w1 + 0.4)) * (0.35 + 0.65 * flick);\n"
    "  float arc2 = smoothstep(0.14, 0.0, abs(w2 - 0.6)) * 0.45;\n"
    "  float pulse = 0.8 + 0.2 * sin(t * 2.0);\n"
    "  float I = smoothstep(0.25, 0.80, n) * 0.95 * pulse + band * 0.45;\n"
    "  vec3 col = mix(vec3(0.06, 0.02, 0.22), vec3(0.30, 0.60, 1.0), clamp(I, 0.0, 1.0));\n"
    "  col += vec3(0.80, 0.95, 1.0) * (arc1 + arc2) * 1.2;\n"
    "  col += vec3(0.90, 0.95, 1.0) * exp(-acos(clamp(view.z, -1.0, 1.0)) * 3.0) * 0.35 * pulse;\n"
    "  return vec4(col * u_bright, 1.0);\n"
    "}\n";

/* ---- 5: ship's computer (u_a, u_b = gauge levels) ---- */
static const char COMP[] =
    U
    "float band(float a, float r0, float r1){ return step(r0, a) * step(a, r1); }\n"
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  float a = acos(clamp(view.z, -1.0, 1.0)), th = atan(view.y, view.x);\n"
    "  float u = th / (2.0 * PI) + 0.5;\n"
    "  vec3 amber = vec3(1.0, 0.60, 0.10), teal = vec3(0.20, 0.90, 0.80), red = vec3(1.0, 0.25, 0.15);\n"
    "  vec3 col = vec3(0.015, 0.02, 0.03);\n"
    "  col += amber * band(a, 0.20, 0.27) * step(0.5, fract((u + u_t * 0.02) * 72.0));\n"
    "  float seg = floor(fract(u - u_t * 0.01) * 24.0);\n"
    "  col += (seg < u_a * 24.0 ? amber : amber * 0.15) * band(a, 0.31, 0.39)\n"
    "         * step(0.15, fract(fract(u - u_t * 0.01) * 24.0));\n"
    "  col += teal * band(a, 0.45, 0.50) * step(0.65, fract((u - u_t * 0.03) * 12.0));\n"
    "  float tk = fract((u + u_t * 0.005) * 120.0);\n"
    "  float major = step(0.8, fract((u + u_t * 0.005) * 12.0));\n"
    "  col += amber * 0.8 * band(a, 0.60, 0.60 + 0.03 + 0.04 * major) * step(0.6, tk);\n"
    "  float seg2 = floor(fract(u + u_t * 0.015) * 40.0);\n"
    "  col += (seg2 < u_b * 40.0 ? teal : teal * 0.12) * band(a, 0.86, 0.92)\n"
    "         * step(0.2, fract(fract(u + u_t * 0.015) * 40.0));\n"
    "  col += red * band(a, 1.12, 1.15) * step(0.92, fract((u - u_t * 0.04) * 8.0));\n"
    "  col += amber * 0.5 * smoothstep(0.004, 0.0, abs(a - 1.2));\n"
    "  col += amber * smoothstep(0.03, 0.0, a) * (0.6 + 0.4 * sin(u_t * 3.0));\n"
    "  col += amber * 0.6 * (smoothstep(0.003, 0.0, abs(view.x)) + smoothstep(0.003, 0.0, abs(view.y)))\n"
    "         * band(a, 0.06, 0.15);\n"
    "  return vec4(col * u_bright, 1.0);\n"
    "}\n";

static const char *GLSL[NSTYLE] = { HOLO, RADAR, WARP, SCAN, CORE, COMP };
static GLuint g_prog[NSTYLE];
static float  g_t, g_sweep, g_travel, g_warp, g_flash, g_cycle;
static DomePoints g_txt[2], g_blip;
static float  g_txt_rot[2];
static time_t g_txt_built;

static int style(void)
{
    int s = (int)lroundf(L.style);
    return s < 0 ? 0 : s >= NSTYLE ? NSTYLE - 1 : s;
}

static float frnd(void) { return (float)rand() / (float)RAND_MAX; }

/* ---------------------------------------------------------------- */
/* Radar contacts                                                     */
/* ---------------------------------------------------------------- */
#define MAX_BLIP 400
typedef struct { float lat, lon; } LL;
static LL     g_sats[MAX_BLIP];
static int    g_nsats;
static time_t g_sats_mt;
static LL     g_fake[14];

static void read_sats(void)
{
    struct stat st;
    const char *path = dome_path("data/sats.txt");
    if (stat(path, &st) != 0 || time(NULL) - st.st_mtime > 60) { g_nsats = 0; return; }
    if (st.st_mtime == g_sats_mt) return;
    g_sats_mt = st.st_mtime;
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[200], c;
    float lat, lon;
    int n = 0;
    while (fgets(line, sizeof line, f) && n < MAX_BLIP)
        if (line[0] != 't' && sscanf(line, " %c %f %f", &c, &lat, &lon) == 3) {
            g_sats[n].lat = lat; g_sats[n].lon = lon; n++;
        }
    fclose(f);
    g_nsats = n;
}

/* distance (km) and bearing (radians, clockwise from north) from home */
static void range_bearing(float lat, float lon, float *km, float *brg)
{
    float p1 = DEG2RAD(L.home_lat), p2 = DEG2RAD(lat), dl = DEG2RAD(lon - L.home_lon);
    float c = sinf(p1) * sinf(p2) + cosf(p1) * cosf(p2) * cosf(dl);
    *km = acosf(fmaxf(-1.0f, fminf(1.0f, c))) * 6371.0f;
    *brg = atan2f(sinf(dl) * cosf(p2), cosf(p1) * sinf(p2) - sinf(p1) * cosf(p2) * cosf(dl));
}

static int build_blips(float omega)
{
    read_sats();
    dome_points_clear(&g_blip);
    int n = 0;
    int live = g_nsats > 0;
    int count = live ? g_nsats : 14;
    for (int i = 0; i < count; i++) {
        float km, brg, a, th;
        if (live) {
            range_bearing(g_sats[i].lat, g_sats[i].lon, &km, &brg);
            if (km > L.range) continue;
            a = km / L.range * 1.25f;
            th = 1.5708f - brg;                       /* north up, east right */
        } else {
            LL *f = &g_fake[i];
            f->lat += 0.0005f * sinf(i * 1.7f); f->lon += 0.0007f * cosf(i * 2.3f);
            a = f->lat; th = f->lon;
        }
        /* bright as the beam passes, then fading until it comes round again */
        float d = fmodf(g_sweep - th, 6.2832f);
        if (d < 0) d += 6.2832f;
        float age = d / fmaxf(omega, 0.01f);
        float glow = expf(-age / 2.5f);
        float s = sinf(a);
        dome_points_push(&g_blip, s * cosf(th), s * sinf(th), cosf(a), 0.6f, 1.0f, 0.6f, 0.15f + 0.85f * glow);
        n++;
    }
    dome_points_upload(&g_blip);
    return n;
}

/* ---------------------------------------------------------------- */
/* Readouts                                                           */
/* ---------------------------------------------------------------- */
static void hexline(char *buf, int n)
{
    static const char *HX = "0123456789ABCDEF";
    for (int i = 0; i < n; i++) buf[i] = (i % 5 == 4) ? ' ' : HX[rand() % 16];
    buf[n] = 0;
}

/* a full ring of text: as many characters as fit round the circle */
static int ring_chars(float radius_deg, float height_deg)
{
    float px = DEG2RAD(height_deg) / 7.0f;
    return (int)(6.2832f * sinf(DEG2RAD(radius_deg)) / (6.0f * px)) - 1;
}

static void build_text(int s, int contacts)
{
    char a[160], b[160];
    for (int i = 0; i < 2; i++) dome_points_clear(&g_txt[i]);
    switch (s) {
    case 0:
        snprintf(a, sizeof a, "SCAN %02d%%   SECTOR %c-%d   LOCK %s", rand() % 100,
                 'A' + rand() % 8, 1 + rand() % 9, rand() % 3 ? "SEARCHING" : "ACQUIRED");
        dome_text_arc(&g_txt[0], a, 80.0f, 90.0f, 3.2f, 0.3f, 0.9f, 1.0f, 0.9f);
        hexline(b, 30);
        dome_text_arc(&g_txt[0], b, 80.0f, -90.0f, 3.0f, 0.3f, 0.9f, 1.0f, 0.7f);
        break;
    case 1:
        snprintf(a, sizeof a, "RANGE %.0f KM   %d CONTACTS%s", L.range, contacts,
                 g_nsats ? "" : "   (SIMULATED)");
        dome_text_arc(&g_txt[0], a, 78.0f, -90.0f, 3.2f, 0.4f, 1.0f, 0.5f, 0.9f);
        dome_text_arc(&g_txt[0], "N", 74.5f, 90.0f, 3.6f, 0.4f, 1.0f, 0.5f, 1.0f);
        break;
    case 3:
        snprintf(a, sizeof a, "SURFACE SCAN   ELEV %.1f KM   TEMP %d K", frnd() * 9.0f, 140 + rand() % 200);
        dome_text_arc(&g_txt[0], a, 80.0f, -90.0f, 3.2f, 0.4f, 1.0f, 0.9f, 0.9f);
        snprintf(b, sizeof b, "ATMOS %d%%   GRAV %.2f G", rand() % 100, 0.3f + frnd() * 1.5f);
        dome_text_arc(&g_txt[0], b, 80.0f, 90.0f, 3.2f, 0.4f, 1.0f, 0.9f, 0.9f);
        break;
    case 5: {
        int n0 = ring_chars(43.0f, 3.0f), n1 = ring_chars(60.0f, 3.0f);
        hexline(a, n0 < 150 ? n0 : 150);
        hexline(b, n1 < 150 ? n1 : 150);
        dome_text_arc(&g_txt[0], a, 43.0f, -90.0f, 3.0f, 1.0f, 0.6f, 0.1f, 0.9f);
        dome_text_arc(&g_txt[1], b, 60.0f, -90.0f, 3.0f, 0.2f, 0.9f, 0.8f, 0.9f);
        break;
    }
    default: break;
    }
    for (int i = 0; i < 2; i++) dome_points_upload(&g_txt[i]);
    g_txt_built = time(NULL);
}

/* ---------------------------------------------------------------- */
static void init(void)
{
    for (int i = 0; i < NSTYLE; i++) g_prog[i] = dome_surface_program_lib(DOME_GLSL_NOISE, GLSL[i]);
    for (int i = 0; i < 2; i++) dome_points_init(&g_txt[i], 40000);
    dome_points_init(&g_blip, MAX_BLIP);
    for (int i = 0; i < 14; i++) { g_fake[i].lat = 0.15f + frnd() * 1.05f; g_fake[i].lon = frnd() * 6.2832f; }
}

/* hyperspace: cruise, spool up, jump, drop back out */
static void warp_update(float dt)
{
    g_cycle = fmodf(g_cycle + dt, 24.0f);
    float c = g_cycle, target, speed;
    if (c < 6.0f)       { target = 0.0f; speed = 0.25f; }
    else if (c < 8.0f)  { target = (c - 6.0f) / 2.0f; speed = 0.25f + 3.0f * target * target; }
    else if (c < 18.0f) { target = 1.0f; speed = 3.2f; }
    else if (c < 19.0f) { target = 1.0f - (c - 18.0f); speed = 0.25f + 3.0f * target; }
    else                { target = 0.0f; speed = 0.25f; }
    g_warp = target;
    g_travel += dt * speed;
    g_flash = (c > 8.0f && c < 8.4f) ? (8.4f - c) / 0.4f * 0.6f
            : (c > 18.6f && c < 19.0f) ? (19.0f - c) / 0.4f * 0.4f : 0.0f;
}

static void update(const DomeFrame *f)
{
    float dt = f->dt * L.speed;
    g_t += dt;
    g_sweep = fmodf(g_sweep + dt * 1.2f, 6.2832f);
    warp_update(dt);
    g_txt_rot[0] += dt * 0.05f;
    g_txt_rot[1] -= dt * 0.03f;
}

static void draw(const DomeFrame *f)
{
    int s = style();
    GLuint p = g_prog[s];
    float ua = 0, ub = 0, ut = g_t;
    if (s == 1) ua = g_sweep;
    if (s == 2) { ua = g_warp; ub = g_flash; ut = g_travel; }
    if (s == 5) { ua = 0.55f + 0.35f * sinf(g_t * 0.4f); ub = 0.5f + 0.4f * sinf(g_t * 0.27f + 1.0f); }

    dome_surface_begin(p, f);
    glUniform1f(glGetUniformLocation(p, "u_t"), ut);
    glUniform1f(glGetUniformLocation(p, "u_scale"), L.scale);
    glUniform1f(glGetUniformLocation(p, "u_bright"), L.bright);
    glUniform1f(glGetUniformLocation(p, "u_a"), ua);
    glUniform1f(glGetUniformLocation(p, "u_b"), ub);
    dome_surface_end();

    mat3 still; m3_identity(still);
    int contacts = 0;
    if (s == 1) {
        contacts = build_blips(1.2f * L.speed);
        dome_draw_points(&g_blip, still, f->fade, 6.0f);
    }
    if (L.text > 0.5f && s != 2 && s != 4) {
        /* readouts refresh every couple of seconds */
        if (time(NULL) - g_txt_built >= 2) build_text(s, contacts);
        if (s == 5) {
            mat3 r0, r1;
            m3_rz(r0, g_txt_rot[0]);
            m3_rz(r1, g_txt_rot[1]);
            dome_draw_points(&g_txt[0], r0, f->fade, 2.5f);
            dome_draw_points(&g_txt[1], r1, f->fade, 2.5f);
        } else {
            dome_draw_points(&g_txt[0], still, f->fade, 2.5f);
        }
    }
}

static int key(SDL_Keycode k, int shift)
{
    (void)shift;
    if (k != SDLK_n) return 0;
    L.style = (float)((style() + 1) % NSTYLE);
    g_txt_built = 0;
    printf("Sci-fi: %s\n", NAMES[style()]);
    return 1;
}

Scene scene_scifi = {
    "SCI-FI", "scifi.cfg", 0, P, (int)(sizeof P / sizeof P[0]),
    init, NULL, update, draw, key,
    "N next style", NULL
};
