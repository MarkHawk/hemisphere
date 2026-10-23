/*
 * weather.c  -  the world's weather. N steps through the styles:
 *
 *   0 live clouds     the Earth turning, with today's clouds lit by the Sun
 *                     and the coastlines glowing through them; still, or
 *                     drifting with the prevailing winds
 *   1 infrared        the same clouds coloured like a forecaster's enhanced
 *                     infrared picture: the brightest, highest tops in red
 *                     and white (a look made from cloud brightness, not
 *                     measured temperatures)
 *   2 over home       zoomed in over home (home_lat, home_lon), from a
 *                     sharper cloud picture cut out round it, with day and
 *                     night and a marker on home
 *   3 aurora          the northern sky from above: the aurora oval from
 *                     NOAA's 30-minute forecast, shimmering on the night side
 *   4 storms          famous tropical storms replayed from NOAA's best-track
 *                     archive: the track drawn as it goes, coloured by
 *                     strength, with the name, date and wind
 *
 * The data comes from tools/orbitfeed.py, which writes it to data/:
 * clouds.jpg (2048 x 1024, every 3 hours, from Matt Eason's live cloud
 * maps, made from EUMETSAT data), clouds_local.jpg and .txt (a sharper
 * cut-out round home), aurora.pgm (360 x 181) and storms.txt. Each
 * style says what is missing if its file is not there yet.
 */
#include "../dome/dome.h"
#include "../third_party/stb_image.h"
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

#define NSTYLE 5
static const char *NAMES[NSTYLE] = { "live clouds", "infrared", "over home", "aurora", "storms" };

static struct { float style, speed, home_lat, home_lon, zoom, drift, cloud_op, coast, lights,
                storm_speed, storm_zoom, text, bright; } L =
    { 0.0f, 1.0f, 51.48f, 0.0f, 1.0f, 1.0f, 0.85f, 0.8f, 1.2f, 18.0f, 1.0f, 1.0f, 1.0f };

static Param P[] = {
    { "style",       &L.style,       1.0f, 0.0f, 4.0f, 0,
      "0 live clouds, 1 infrared, 2 over home, 3 aurora, 4 storms (N)" },
    { "speed",       &L.speed,       0.1f, 0.0f, 4.0f, 0, "speed of the drifting and shimmer" },
    { "home_lat",    &L.home_lat,    0.5f, -80.0f, 80.0f, 0, "home latitude (north +); orbitfeed cuts the sharp picture round it" },
    { "home_lon",    &L.home_lon,    0.5f, -180.0f, 180.0f, 0, "home longitude (east +)" },
    { "zoom",        &L.zoom,        0.25f, 1.0f, 6.0f, 0, "over home: zoom in (1 = the whole side of the Earth)" },
    { "drift",       &L.drift,       1.0f, 0.0f, 1.0f, 0, "clouds: 0 still, 1 drifting with the winds" },
    { "cloud_op",    &L.cloud_op,    0.05f, 0.1f, 1.0f, 0, "how thick the clouds look" },
    { "coast",       &L.coast,       0.1f, 0.0f, 2.0f, 0, "coastlines glowing through the clouds (0 = off)" },
    { "lights",      &L.lights,      0.1f, 0.0f, 3.0f, 0, "city lights on the night side" },
    { "storm_speed", &L.storm_speed, 1.0f, 2.0f, 60.0f, 0, "storms: hours of the storm per second" },
    { "storm_zoom",  &L.storm_zoom,  0.25f, 1.0f, 4.0f, 0, "storms: zoom in on the storm (1 = the whole side of the Earth)" },
    { "text",        &L.text,        1.0f, 0.0f, 1.0f, 0, "show the text: 0 off, 1 on" },
    { "bright",      &L.bright,      0.05f, 0.2f, 2.0f, 0, "brightness" },
};

/* ---------------------------------------------------------------- */
/* shaders                                                           */
/* ---------------------------------------------------------------- */
static const char LIB[] =
    "uniform sampler2D u_day, u_night, u_cloud, u_mask, u_local, u_aur;\n"
    "uniform float u_t, u_bright, u_lights, u_cloud_op, u_coast, u_flow;\n"
    "uniform vec4 u_has;\n"                  /* day, night, clouds, mask */
    "uniform vec4 u_has2;\n"                 /* local, aurora */
    "uniform vec3 u_sun;\n"
    "uniform vec4 u_lb;\n"                   /* local picture: lon0, lon1, lat0, lat1 (radians) */
    "uniform vec4 u_cam;\n"                  /* view centre lat, lon (radians), zoom, use it */
    "vec2 lluv(float lat, float lon){ return vec2(lon / 6.2831853 + 0.5, 0.5 - lat / 3.1415927); }\n"
    /* the Earth-fixed direction for a point of the dome: either the turning
     * globe (dir), or a view centred on u_cam and zoomed in */
    "vec3 earth(vec3 dir, vec3 view){\n"
    "  if (u_cam.w < 0.5) return dir;\n"
    "  float th = acos(clamp(view.z, -1.0, 1.0)) / u_cam.z;\n"
    "  float ph = atan(view.y, view.x);\n"
    "  vec3 v = vec3(sin(th) * cos(ph), sin(th) * sin(ph), cos(th));\n"
    "  float la = u_cam.x, lo = u_cam.y;\n"
    "  vec3 up = vec3(cos(la) * sin(lo), sin(la), cos(la) * cos(lo));\n"
    "  vec3 ea = vec3(cos(lo), 0.0, -sin(lo));\n"
    "  vec3 no = cross(up, ea);\n"
    "  return normalize(ea * v.x + no * v.y + up * v.z);\n"
    "}\n"
    "float cloudsat(vec2 uv, float lat, float lon){\n"
    "  if (u_flow < 0.5) return texture2D(u_cloud, uv).r;\n"
    "  float la = abs(lat);\n"
    "  float wind = mix(-0.6, 1.0, smoothstep(0.42, 0.62, la));\n"
    "  wind = mix(wind, -0.4, smoothstep(1.0, 1.15, la));\n"
    "  vec2 vel = vec2(wind * 0.006, 0.0015 * sin(lon * 3.0 + lat * 5.0));\n"
    "  float ct = u_t / 10.0;\n"
    "  float p0 = fract(ct), p1 = fract(ct + 0.5);\n"
    "  float c0 = texture2D(u_cloud, uv - vel * (p0 - 0.5)).r;\n"
    "  float c1 = texture2D(u_cloud, uv - vel * (p1 - 0.5)).r;\n"
    "  return mix(c1, c0, 1.0 - abs(2.0 * p0 - 1.0));\n"
    "}\n"
    /* the sharper local picture where it covers, else the world one */
    "float cloudat(vec2 uv, float lat, float lon){\n"
    "  if (u_has2.x > 0.5) {\n"
    "    float dl = mod(lon - u_lb.x + 3.1415927, 6.2831853) - 3.1415927;\n"
    "    vec2 q = vec2(dl / (u_lb.y - u_lb.x), (u_lb.w - lat) / (u_lb.w - u_lb.z));\n"
    "    if (q.x > 0.0 && q.x < 1.0 && q.y > 0.0 && q.y < 1.0) {\n"
    "      float e = min(min(q.x, 1.0 - q.x), min(q.y, 1.0 - q.y));\n"
    "      return mix(cloudsat(uv, lat, lon), texture2D(u_local, q).r, smoothstep(0.0, 0.05, e));\n"
    "    }\n"
    "  }\n"
    "  return cloudsat(uv, lat, lon);\n"
    "}\n"
    "float coastline(vec2 uv){\n"
    "  if (u_has.w < 0.5) return 0.0;\n"
    "  return texture2D(u_mask, uv).a;\n"                     /* baked: land in red, coast in alpha */
    "}\n"
    "vec3 ground(vec3 e, vec2 uv, out float lit){\n"
    "  vec3 day = u_has.x > 0.5 ? texture2D(u_day, uv).rgb : vec3(0.05, 0.16, 0.35);\n"
    "  vec3 night = u_has.y > 0.5 ? texture2D(u_night, uv).rgb : vec3(0.0);\n"
    "  float d = dot(e, u_sun);\n"
    "  lit = smoothstep(-0.10, 0.15, d);\n"
    "  return day * (0.05 + 0.95 * lit) + night * (1.0 - smoothstep(-0.20, 0.05, d)) * u_lights;\n"
    "}\n";

/* 0 live clouds and 2 over home (u_cam on): the lit Earth, clouds, coasts */
static const char LIVE[] =
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  vec3 e = earth(dir, view);\n"
    "  float lat = asin(clamp(e.y, -1.0, 1.0)), lon = atan(e.x, e.z);\n"
    "  vec2 uv = lluv(lat, lon);\n"
    "  float lit;\n"
    "  vec3 col = ground(e, uv, lit) * u_bright;\n"
    "  float cl = 0.0;\n"
    "  if (u_has.z > 0.5) {\n"
    "    cl = smoothstep(0.12, 0.85, cloudat(uv, lat, lon)) * u_cloud_op;\n"
    "    col = mix(col, vec3(0.95, 0.97, 1.0) * (0.03 + 0.97 * lit) * u_bright, cl);\n"
    "  }\n"
    "  col += vec3(0.35, 0.85, 1.0) * coastline(uv) * u_coast * (0.25 + 0.75 * cl) * (0.35 + 0.65 * lit) * (u_cam.w > 0.5 ? 0.45 : 1.0);\n"
    "  if (u_cam.w < 0.5) {\n"
    "    float rim = pow(1.0 - clamp(view.z, 0.0, 1.0), 3.0);\n"
    "    col += vec3(0.25, 0.5, 1.0) * rim * 0.35 * (0.3 + 0.7 * lit);\n"
    "  } else {\n"                                                  /* home: a pulsing marker */
    "    float r = acos(clamp(view.z, -1.0, 1.0));\n"
    "    float ring = abs(r - 0.035 - 0.01 * sin(u_t * 3.0));\n"
    "    col = mix(col, vec3(1.0, 0.25, 0.2), smoothstep(0.008, 0.0, ring));\n"
    "    if (r < 0.008) col = vec3(1.0, 0.9, 0.3);\n"
    "    vec2 g = vec2(lat, lon) * 57.29578 / 5.0;\n"                 /* faint 5 degree grid */
    "    vec2 gf = abs(fract(g) - 0.5);\n"
    "    col += vec3(0.08, 0.10, 0.14) * smoothstep(0.47, 0.5, max(gf.x, gf.y)) * u_bright;\n"
    "  }\n"
    "  return vec4(col, 1.0);\n"
    "}\n";

/* 1 infrared: cloud brightness through an enhancement palette */
static const char IR[] =
    "vec3 irpal(float v){\n"
    "  if (v < 0.30) return vec3(v * 1.6);\n"
    "  if (v < 0.48) return mix(vec3(0.15, 0.35, 0.65), vec3(0.15, 0.75, 0.95), (v - 0.30) / 0.18);\n"
    "  if (v < 0.62) return mix(vec3(0.15, 0.80, 0.30), vec3(0.85, 0.95, 0.20), (v - 0.48) / 0.14);\n"
    "  if (v < 0.76) return mix(vec3(1.00, 0.75, 0.10), vec3(1.00, 0.25, 0.10), (v - 0.62) / 0.14);\n"
    "  if (v < 0.88) return mix(vec3(0.85, 0.05, 0.15), vec3(0.75, 0.10, 0.75), (v - 0.76) / 0.12);\n"
    "  return vec3(1.0);\n"
    "}\n"
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  float lat = asin(clamp(dir.y, -1.0, 1.0)), lon = atan(dir.x, dir.z);\n"
    "  vec2 uv = lluv(lat, lon);\n"
    "  vec3 col = vec3(0.02, 0.03, 0.06);\n"
    "  if (u_has.w > 0.5) col += vec3(0.07, 0.08, 0.07) * smoothstep(0.3, 0.7, texture2D(u_mask, uv).r);\n"
    "  if (u_has.z > 0.5) {\n"
    "    float v = cloudsat(uv, lat, lon);\n"
    "    col = mix(col, irpal(v), smoothstep(0.08, 0.2, v));\n"
    "  }\n"
    "  col += vec3(0.9) * coastline(uv) * u_coast * 0.5;\n"
    "  vec2 g = abs(fract(vec2(lat, lon) * 57.29578 / 15.0) - 0.5);\n"
    "  col += vec3(0.05) * smoothstep(0.485, 0.5, max(g.x, g.y));\n"
    "  return vec4(col * u_bright, 1.0);\n"
    "}\n";

/* 3 aurora: looking down on the north, the oval from the forecast */
static const char AUR[] =
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  vec3 e = earth(dir, view);\n"
    "  float lat = asin(clamp(e.y, -1.0, 1.0)), lon = atan(e.x, e.z);\n"
    "  vec2 uv = lluv(lat, lon);\n"
    "  float lit;\n"
    "  vec3 col = ground(e, uv, lit) * u_bright * 0.8;\n"
    "  col += vec3(0.3, 0.6, 1.0) * coastline(uv) * u_coast * 0.3;\n"
    "  if (u_has2.y > 0.5) {\n"
    "    float lo = mod(lon * 57.29578 + 360.0, 360.0);\n"         /* the grid: lon 0..359, lat -90..90 */
    "    float a = texture2D(u_aur, vec2((lo + 0.5) / 360.0, (lat * 57.29578 + 90.5) / 181.0)).r;\n"
    "    float sh = 0.55 + 0.45 * sin(lon * 40.0 + u_t * 1.7 + 3.0 * sin(lat * 30.0 - u_t * 0.6));\n"
    "    float dark = 1.0 - lit;\n"
    "    a = smoothstep(0.03, 0.5, a) * (0.5 + 0.5 * sh) * (0.25 + 0.75 * dark);\n"
    "    col += mix(vec3(0.1, 1.0, 0.35), vec3(1.0, 0.2, 0.45), smoothstep(0.6, 1.0, a)) * a * 1.4;\n"
    "  }\n"
    "  return vec4(col, 1.0);\n"
    "}\n";

/* 4 storms: a plain, softly lit Earth; the tracks are dots */
static const char STORM[] =
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  vec3 e = earth(dir, view);\n"
    "  float lat = asin(clamp(e.y, -1.0, 1.0)), lon = atan(e.x, e.z);\n"
    "  vec2 uv = lluv(lat, lon);\n"
    "  vec3 col = (u_has.x > 0.5 ? texture2D(u_day, uv).rgb : vec3(0.05, 0.16, 0.35)) * 0.55;\n"
    "  col += vec3(0.3, 0.7, 1.0) * coastline(uv) * u_coast * 0.4;\n"
    "  vec2 g = abs(fract(vec2(lat, lon) * 57.29578 / 10.0) - 0.5);\n"
    "  col += vec3(0.06, 0.08, 0.1) * smoothstep(0.48, 0.5, max(g.x, g.y));\n"
    "  return vec4(col * u_bright, 1.0);\n"
    "}\n";

static const char *GLSL[NSTYLE] = { LIVE, IR, LIVE, AUR, STORM };

/* ---------------------------------------------------------------- */
/* data                                                              */
/* ---------------------------------------------------------------- */
static GLuint g_prog[NSTYLE], g_day, g_night, g_cloud, g_mask, g_local, g_aur;
static int    g_has_day, g_has_night, g_has_cloud, g_has_mask, g_has_local, g_has_aur;
static time_t g_mt_cloud, g_mt_local, g_mt_aur, g_mt_storm;
static float  g_lb[4];
static float  g_t;
static DomePoints g_pts, g_txt;

/* case-blind "does n contain w", without strcasestr (not on Windows) */
static const char *has(const char *n, const char *w)
{
    size_t a = strlen(n), b = strlen(w);
    for (size_t i = 0; i + b <= a; i++) if (!strncasecmp(n + i, w, b)) return n + i;
    return NULL;
}

static time_t mtime(const char *path) { struct stat st; return stat(path, &st) == 0 ? st.st_mtime : 0; }

static void tex_setup(GLuint *t)
{
    glGenTextures(1, t);
    glBindTexture(GL_TEXTURE_2D, *t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

static int load_grey(const char *path, GLuint tex)
{
    int w, h, ch;
    unsigned char *img = stbi_load(path, &w, &h, &ch, 1);
    if (!img) return 0;
    GLint max = 2048; glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max);
    int ok = w <= max && h <= max;
    if (ok) {
        glBindTexture(GL_TEXTURE_2D, tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, w, h, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, img);
    } else printf("Weather: %s is too big (%dx%d)\n", path, w, h);
    stbi_image_free(img);
    return ok;
}

/* the aurora grid: a plain binary PGM (stb_image here only reads JPEG and PNG) */
static int load_pgm(const char *path, GLuint tex)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    int w = 0, h = 0, mx = 0;
    if (fscanf(f, "P5 %d %d %d", &w, &h, &mx) != 3 || w != 360 || h != 181) { fclose(f); return 0; }
    fgetc(f);
    static unsigned char px[360 * 181];
    int ok = fread(px, 1, sizeof px, f) == sizeof px;
    fclose(f);
    if (!ok) return 0;
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, w, h, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, px);
    return 1;
}

static int load_rgb(const char *path, GLuint tex)
{
    int w, h, ch;
    unsigned char *img = stbi_load(path, &w, &h, &ch, 3);
    if (!img) return 0;
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, img);
    stbi_image_free(img);
    return 1;
}

/* the day and night maps from textures/, and a land mask from the day
 * map's colours (sea is dark and blue) for the glowing coastlines */
static void load_earth(void)
{
    DIR *d = opendir(dome_path("textures"));
    char day[300] = "";
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)))
            if (has(e->d_name, "earth") && has(e->d_name, "day") && !has(e->d_name, "night")) snprintf(day, sizeof day, "%s", e->d_name);
        closedir(d);
    }
    if (!day[0]) { printf("Weather: no Earth day map in textures/\n"); return; }
    char path[600];
    snprintf(path, sizeof path, "%s/%s", dome_path("textures"), day);
    g_has_day = load_rgb(path, g_day);
    int w, h, ch;
    unsigned char *img = stbi_load(path, &w, &h, &ch, 3);
    if (img) {
        enum { MW = 1024, MH = 512 };
        unsigned char *m = malloc(MW * MH), *la = malloc(MW * MH * 2);
        for (int y = 0; y < MH; y++)
            for (int x = 0; x < MW; x++) {
                const unsigned char *p = img + ((size_t)(y * h / MH) * w + (size_t)(x * w / MW)) * 3;
                int sea = p[2] > p[0] + 25 && p[2] + 5 > p[1] && p[0] + p[1] + p[2] < 420;
                m[y * MW + x] = sea ? 0 : 255;
            }
        stbi_image_free(img);
        /* land, and the coastline baked in (a cell next to the other kind),
         * so the shaders need one look-up instead of three */
        for (int y = 0; y < MH; y++)
            for (int x = 0; x < MW; x++) {
                int v = m[y * MW + x], edge = 0;
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++) {
                        int yy = y + dy < 0 ? 0 : y + dy >= MH ? MH - 1 : y + dy, xx = (x + dx + MW) % MW;
                        if (m[yy * MW + xx] != v) edge = 1;
                    }
                la[(y * MW + x) * 2] = (unsigned char)v;
                la[(y * MW + x) * 2 + 1] = edge ? 255 : 0;
            }
        glBindTexture(GL_TEXTURE_2D, g_mask);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, MW, MH, 0, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, la);
        free(m); free(la);
        g_has_mask = 1;
    }
    const char *at = has(day, "day");
    if (at) {
        snprintf(path, sizeof path, "%s/%.*snight%s", dome_path("textures"), (int)(at - day), day, at + 3);
        g_has_night = load_rgb(path, g_night);
    }
}

/* ---- storms ------------------------------------------------------ */
#define MAX_STORMS 40
#define MAX_PTS 6000
typedef struct { float lat, lon, wind; int cat; long t; } SPt;
typedef struct { char name[24]; int season, first, n; } Storm;
static Storm g_st[MAX_STORMS];
static SPt   g_sp[MAX_PTS];
static int   g_nst, g_nsp, g_cur;
static float g_st_clock;              /* hours into the current storm */
static float g_clat, g_clon;          /* where the view is centred */

static void read_storms(void)
{
    FILE *f = fopen(dome_path("data/storms.txt"), "r");
    g_nst = g_nsp = 0;
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        if (line[0] == 'S' && g_nst < MAX_STORMS) {
            Storm *s = &g_st[g_nst];
            if (sscanf(line + 1, "%23s %d", s->name, &s->season) == 2) { s->first = g_nsp; s->n = 0; g_nst++; }
        } else if (line[0] == 'P' && g_nst > 0 && g_nsp < MAX_PTS) {
            SPt *p = &g_sp[g_nsp];
            if (sscanf(line + 1, "%ld %f %f %f %d", &p->t, &p->lat, &p->lon, &p->wind, &p->cat) == 5) { g_nsp++; g_st[g_nst - 1].n++; }
        }
    }
    fclose(f);
    while (g_nst > 0 && g_st[g_nst - 1].n < 2) g_nst--;
    printf("Weather: %d storms to replay\n", g_nst);
    g_cur = 0; g_st_clock = 0;
}

static const float CATCOL[7][3] = {   /* depression, storm, then categories 1 to 5 */
    { 0.35f, 0.6f, 1.0f }, { 0.3f, 0.95f, 0.9f }, { 1.0f, 1.0f, 0.5f }, { 1.0f, 0.75f, 0.25f },
    { 1.0f, 0.4f, 0.2f }, { 1.0f, 0.2f, 0.45f }, { 1.0f, 0.55f, 1.0f } };

static void poll_data(void)
{
    const char *cp = dome_path("data/clouds.jpg");
    time_t t = mtime(cp);
    if (t && t != g_mt_cloud) { g_mt_cloud = t; if (load_grey(cp, g_cloud)) g_has_cloud = 1; }
    t = mtime(dome_path("data/clouds_local.jpg"));
    if (t && t != g_mt_local) {
        g_mt_local = t;
        FILE *f = fopen(dome_path("data/clouds_local.txt"), "r");
        float b[4];
        if (f && fscanf(f, "%f %f %f %f", &b[0], &b[1], &b[2], &b[3]) == 4 &&
            load_grey(dome_path("data/clouds_local.jpg"), g_local)) {
            for (int i = 0; i < 4; i++) g_lb[i] = DEG2RAD(b[i]);
            g_has_local = 1;
        }
        if (f) fclose(f);
    }
    t = mtime(dome_path("data/aurora.pgm"));
    if (t && t != g_mt_aur) { g_mt_aur = t; if (load_pgm(dome_path("data/aurora.pgm"), g_aur)) g_has_aur = 1; }
    t = mtime(dome_path("data/storms.txt"));
    if (t && t != g_mt_storm) { g_mt_storm = t; read_storms(); }
}

/* sub-solar point, Earth-fixed (as in orbit watch) */
static void sun_dir(float out[3])
{
    double d = (double)time(NULL) / 86400.0 + 2440587.5 - 2451545.0;
    double R = 3.14159265358979 / 180.0;
    double g = (357.529 + 0.98560028 * d) * R;
    double q = 280.459 + 0.98564736 * d;
    double Lm = (q + 1.915 * sin(g) + 0.020 * sin(2 * g)) * R;
    double e = (23.439 - 0.00000036 * d) * R;
    double ra = atan2(cos(e) * sin(Lm), cos(Lm));
    double dec = asin(sin(e) * sin(Lm));
    double gmst = fmod(18.697374558 + 24.06570982441908 * d, 24.0) * 15.0 * R;
    dome_sph((float)dec, (float)(ra - gmst), out);
}

static int style(void)
{
    int s = (int)lroundf(L.style);
    return s < 0 ? 0 : s >= NSTYLE ? NSTYLE - 1 : s;
}

/* an Earth point (lat, lon, radians) onto the dome for a view centred on
 * (clat, clon) and zoomed by z; 0 if it is out of sight */
static int to_dome(float lat, float lon, float clat, float clon, float z, float out[3])
{
    float e[3], up[3], ea[3], no[3];
    dome_sph(lat, lon, e);
    dome_sph(clat, clon, up);
    ea[0] = cosf(clon); ea[1] = 0; ea[2] = -sinf(clon);
    no[0] = up[1] * ea[2] - up[2] * ea[1]; no[1] = up[2] * ea[0] - up[0] * ea[2]; no[2] = up[0] * ea[1] - up[1] * ea[0];
    float x = e[0] * ea[0] + e[1] * ea[1] + e[2] * ea[2];
    float y = e[0] * no[0] + e[1] * no[1] + e[2] * no[2];
    float c = e[0] * up[0] + e[1] * up[1] + e[2] * up[2];
    float th = acosf(fmaxf(-1.0f, fminf(1.0f, c))) * z;
    if (th > DEG2RAD(96.0f)) return 0;
    float ph = atan2f(y, x);
    out[0] = sinf(th) * cosf(ph); out[1] = sinf(th) * sinf(ph); out[2] = cosf(th);
    return 1;
}

static float lerpf(float a, float b, float f) { return a + (b - a) * f; }
static float lon_lerp(float a, float b, float f)
{
    float d = b - a;
    while (d > 180) d -= 360;
    while (d < -180) d += 360;
    return a + d * f;
}

/* the current storm: its track so far as dots, and a spinning symbol */
static void storm_build(float dt, float *clat, float *clon)
{
    dome_points_clear(&g_pts);
    dome_points_clear(&g_txt);
    if (g_nst == 0) {
        if (L.text > 0.5f) {
            dome_text_arc(&g_txt, "NO STORM DATA YET", 30.0f, 90.0f, 5.0f, 1.0f, 0.8f, 0.4f, 1.0f);
            dome_text_arc(&g_txt, "IS ORBITFEED RUNNING?", 30.0f, -90.0f, 5.0f, 1.0f, 0.8f, 0.4f, 1.0f);
        }
        *clat = DEG2RAD(25.0f); *clon = DEG2RAD(-70.0f);
        return;
    }
    Storm *s = &g_st[g_cur % g_nst];
    SPt *p = &g_sp[s->first];
    float hours = (p[s->n - 1].t - p[0].t) / 3600.0f;
    g_st_clock += dt * L.storm_speed;
    if (g_st_clock > hours + 4.0f * L.storm_speed) { g_cur = (g_cur + 1) % g_nst; g_st_clock = 0; s = &g_st[g_cur]; p = &g_sp[s->first];
                                                     hours = (p[s->n - 1].t - p[0].t) / 3600.0f; }
    float now = fminf(g_st_clock, hours);
    long tnow = p[0].t + (long)(now * 3600.0f);
    /* where it is now */
    int k = 0;
    while (k < s->n - 2 && p[k + 1].t <= tnow) k++;
    float f = p[k + 1].t > p[k].t ? fminf(1.0f, (float)(tnow - p[k].t) / (float)(p[k + 1].t - p[k].t)) : 0.0f;
    float lat = lerpf(p[k].lat, p[k + 1].lat, f), lon = lon_lerp(p[k].lon, p[k + 1].lon, f);
    float wind = lerpf(p[k].wind, p[k + 1].wind, f);
    int cat = f < 0.5f ? p[k].cat : p[k + 1].cat;
    /* the view glides after it */
    float tl = DEG2RAD(lat), to = DEG2RAD(lon);
    float kk = 1.0f - expf(-dt * 1.5f);
    if (g_st_clock < 0.01f) { g_clat = tl; g_clon = to; }
    g_clat += (tl - g_clat) * kk;
    float dl = to - g_clon;
    while (dl > DOME_PI) dl -= 2 * DOME_PI;
    while (dl < -DOME_PI) dl += 2 * DOME_PI;
    g_clon += dl * kk;
    *clat = g_clat; *clon = g_clon;
    const float Z = L.storm_zoom;
    /* the track so far, a dot every 0.25 degrees */
    for (int i = 0; i + 1 < s->n && p[i].t <= tnow; i++) {
        float la0 = p[i].lat, lo0 = p[i].lon, la1 = p[i + 1].lat, lo1 = p[i + 1].lon;
        long t1 = p[i + 1].t;
        float end = 1.0f;
        if (t1 > tnow) end = (float)(tnow - p[i].t) / (float)(t1 - p[i].t);
        float dlo = lo1 - lo0; while (dlo > 180) dlo -= 360; while (dlo < -180) dlo += 360;
        float len = sqrtf((la1 - la0) * (la1 - la0) + dlo * dlo);
        int n = 1 + (int)(len / 0.25f);
        const float *c = CATCOL[p[i].cat < -1 ? 0 : p[i].cat + 1 > 6 ? 6 : p[i].cat + 1];
        for (int j = 0; j < n; j++) {
            float ff = (float)j / n;
            if (ff > end) break;
            float v[3];
            if (to_dome(DEG2RAD(lerpf(la0, la1, ff)), DEG2RAD(lo0 + dlo * ff), g_clat, g_clon, Z, v))
                dome_points_push(&g_pts, v[0], v[1], v[2], c[0], c[1], c[2], 1.0f);
        }
    }
    /* the storm itself: spiral arms turning round the eye, bigger when stronger */
    const float *c = CATCOL[cat < -1 ? 0 : cat + 1 > 6 ? 6 : cat + 1];
    float size = 1.2f + 0.035f * wind;                  /* degrees */
    for (int arm = 0; arm < 3; arm++)
        for (int j = 0; j < 40; j++) {
            float r = size * j / 40.0f, a = g_t * 4.0f + arm * 2.0944f + j * 0.16f;
            float v[3];
            if (to_dome(DEG2RAD(lat + r * sinf(a)), DEG2RAD(lon + r * cosf(a) / cosf(DEG2RAD(lat))), g_clat, g_clon, Z, v))
                dome_points_push(&g_pts, v[0], v[1], v[2], 0.9f + 0.1f * c[0], 0.9f, 0.95f, 1.0f - j / 50.0f);
        }
    /* the words */
    if (L.text > 0.5f) {
        char buf[64], when[24];
        time_t tt = (time_t)tnow;
        struct tm g = *gmtime(&tt);
        strftime(when, sizeof when, "%d %b %Y", &g);
        snprintf(buf, sizeof buf, "%s %d", s->name, s->season);
        dome_text_arc(&g_txt, buf, 78.0f, 90.0f, 5.0f, 1.0f, 1.0f, 1.0f, 1.0f);
        static const char *CN[7] = { "DEPRESSION", "TROPICAL STORM", "CATEGORY 1", "CATEGORY 2", "CATEGORY 3", "CATEGORY 4", "CATEGORY 5" };
        int ci = cat < -1 ? 0 : cat + 1 > 6 ? 6 : cat + 1;
        snprintf(buf, sizeof buf, "%s  %.0f KT  %s", CN[ci], wind, when);
        for (char *q = buf; *q; q++) if (*q >= 'a' && *q <= 'z') *q -= 32;
        dome_text_arc(&g_txt, buf, 78.0f, -90.0f, 4.0f, c[0], c[1], c[2], 1.0f);
    }
}

static void text_age(const char *what, time_t t)
{
    dome_points_clear(&g_txt);
    if (L.text < 0.5f) return;
    char buf[64];
    if (!t) snprintf(buf, sizeof buf, "NO %s YET - IS ORBITFEED RUNNING?", what);
    else {
        struct tm g = *gmtime(&t);
        char hm[16]; strftime(hm, sizeof hm, "%H:%M", &g);
        snprintf(buf, sizeof buf, "%s %s UTC", what, hm);
    }
    dome_text_arc(&g_txt, buf, 80.0f, -90.0f, 4.0f, 0.85f, 0.9f, 1.0f, 0.9f);
}

/* ---------------------------------------------------------------- */
static void init(void)
{
    for (int i = 0; i < NSTYLE; i++) g_prog[i] = dome_surface_program_lib(LIB, GLSL[i]);
    GLuint *t[6] = { &g_day, &g_night, &g_cloud, &g_mask, &g_local, &g_aur };
    for (int i = 0; i < 6; i++) tex_setup(t[i]);
    load_earth();
    dome_points_init(&g_pts, 20000);
    dome_points_init(&g_txt, 20000);
    poll_data();
}

static float g_cam[4];
static float g_since;

static void update(const DomeFrame *f)
{
    g_t += f->dt * L.speed;
    if ((g_since += f->dt) > 10.0f) { g_since = 0; poll_data(); }
    int s = style();
    g_cam[3] = 0;
    if (s == 2) { g_cam[0] = DEG2RAD(L.home_lat); g_cam[1] = DEG2RAD(L.home_lon); g_cam[2] = L.zoom; g_cam[3] = 1; }
    if (s == 3) { g_cam[0] = DEG2RAD(78.0f); g_cam[1] = DEG2RAD(L.home_lon); g_cam[2] = 1.1f; g_cam[3] = 1; }
    if (s == 4) { storm_build(fminf(f->dt, 0.1f), &g_cam[0], &g_cam[1]); g_cam[2] = L.storm_zoom; g_cam[3] = 1; }
    else {
        static int last = -1; static time_t last_t = 0;
        time_t t = s == 3 ? g_mt_aur : g_mt_cloud;
        if (s != last || t != last_t) {
            text_age(s == 3 ? "AURORA FORECAST" : s == 1 ? "INFRARED LOOK" : "CLOUDS", t);
            last = s; last_t = t;
        }
    }
    dome_points_upload(&g_pts);
    dome_points_upload(&g_txt);
}

static void bind(GLuint p, const char *name, GLuint tex, int unit)
{
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(glGetUniformLocation(p, name), unit);
}

static void draw(const DomeFrame *f)
{
    int s = style();
    GLuint p = g_prog[s];
    float sun[3]; sun_dir(sun);
    dome_surface_begin(p, f);
    bind(p, "u_day", g_day, 1); bind(p, "u_night", g_night, 2); bind(p, "u_cloud", g_cloud, 3);
    bind(p, "u_mask", g_mask, 4); bind(p, "u_local", g_local, 5); bind(p, "u_aur", g_aur, 6);
    glActiveTexture(GL_TEXTURE0);
    glUniform1f(glGetUniformLocation(p, "u_t"), g_t);
    glUniform1f(glGetUniformLocation(p, "u_bright"), L.bright);
    glUniform1f(glGetUniformLocation(p, "u_lights"), L.lights);
    glUniform1f(glGetUniformLocation(p, "u_cloud_op"), L.cloud_op);
    glUniform1f(glGetUniformLocation(p, "u_coast"), L.coast);
    glUniform1f(glGetUniformLocation(p, "u_flow"), L.drift > 0.5f ? 1.0f : 0.0f);
    glUniform4f(glGetUniformLocation(p, "u_has"), (float)g_has_day, (float)g_has_night, (float)g_has_cloud, (float)g_has_mask);
    glUniform4f(glGetUniformLocation(p, "u_has2"), s == 2 ? (float)g_has_local : 0.0f, (float)g_has_aur, 0, 0);
    glUniform3fv(glGetUniformLocation(p, "u_sun"), 1, sun);
    glUniform4fv(glGetUniformLocation(p, "u_lb"), 1, g_lb);
    glUniform4fv(glGetUniformLocation(p, "u_cam"), 1, g_cam);
    dome_surface_end();
    mat3 still; m3_identity(still);
    float fa = f->fade;
    if (s == 4) {
        dome_draw_points(&g_pts, still, fa * 0.3f, 7.0f);
        dome_draw_points(&g_pts, still, fa, 3.0f);
    }
    dome_draw_points(&g_txt, still, fa, 2.6f);
}

static int key(SDL_Keycode k, int shift)
{
    (void)shift;
    if (k != SDLK_n) return 0;
    L.style = (float)((style() + 1) % NSTYLE);
    printf("Weather: %s\n", NAMES[style()]);
    return 1;
}

Scene scene_weather = {
    "WEATHER", "weather.cfg", 0, P, (int)(sizeof P / sizeof P[0]),
    init, NULL, update, draw, key,
    "N next style", NULL
};
