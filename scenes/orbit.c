/*
 * orbit.c  -  orbit watch: the live Earth with the ISS, satellites and
 * earthquakes.
 *
 * The Earth uses the day map from the textures folder (a file with
 * "earth" and "day" in its name), with the night map's city lights on
 * the real night side: the Sun's position is worked out from the clock.
 *
 * Live data comes from tools/orbitfeed.py, which writes small files to
 * data/ beside the program. This scene re-reads them when they change:
 *   - the ISS: a bright pulsing marker, its next orbit drawn ahead of it
 *     and a short trail behind
 *   - other satellites as small dots: yellow stations, white bright
 *     satellites, green GPS, blue weather satellites. Between updates
 *     each one is moved on smoothly by its own speed.
 *   - earthquakes from the last day or so as rings that pulse outwards,
 *     sized and coloured by magnitude
 * Three lines of text across the equator, on a dark band so they read
 * over the map, give the ISS's position, altitude and speed, and a count
 * of satellites and quakes. The text stays put while the Earth turns.
 *
 * N cycles the view: turning slowly, following the ISS, or held over
 * home (set home_lat and home_lon).
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

static struct { float view, home_lat, home_lon, sats, quakes, lights, text, band, bright; } L =
    { 0.0f, 51.48f, 0.0f, 1.0f, 1.0f, 1.2f, 1.0f, 0.65f, 1.0f };

static Param P[] = {
    { "view",     &L.view,     1.0f,   0.0f, 2.0f, 0,
      "0 turning, 1 follow the ISS, 2 held over home (N cycles)" },
    { "home_lat", &L.home_lat, 0.5f, -90.0f, 90.0f, 0, "home latitude for view 2 (north +)" },
    { "home_lon", &L.home_lon, 0.5f, -180.0f, 180.0f, 0, "home longitude for view 2 (east +)" },
    { "sats",     &L.sats,     0.1f,   0.0f, 2.0f, 0, "brightness of the satellites (0 = hide)" },
    { "quakes",   &L.quakes,   0.1f,   0.0f, 2.0f, 0, "brightness of the earthquakes (0 = hide)" },
    { "lights",   &L.lights,   0.1f,   0.0f, 3.0f, 0, "city lights on the night side" },
    { "text",     &L.text,     1.0f,   0.0f, 1.0f, 0, "show the text: 0 off, 1 on" },
    { "band",     &L.band,     0.05f,  0.0f, 1.0f, 0, "darkness of the band behind the text" },
    { "bright",   &L.bright,   0.05f,  0.2f, 2.0f, 0, "brightness of the Earth" },
};

static const char *EARTH_GLSL =
    "uniform sampler2D u_day, u_night;\n"
    "uniform float u_has_day, u_has_night, u_lights, u_bright, u_band, u_bw, u_bh;\n"
    "uniform mat3  u_vinv;\n"               /* dome -> upright globe frame */
    "uniform vec3  u_sun;\n"                 /* Sun direction, Earth-fixed */
    "vec4 surface(vec3 dir, vec3 view){\n"
    "  float lat = asin(clamp(dir.y, -1.0, 1.0));\n"
    "  float lon = atan(dir.x, dir.z);\n"
    "  vec2 uv = vec2(lon / (2.0 * PI) + 0.5, 0.5 - lat / PI);\n"
    "  vec3 day = u_has_day > 0.5 ? texture2D(u_day, uv).rgb\n"
    /* no map: a plain blue globe with a faint grid */
    "      : vec3(0.05, 0.16, 0.35) + vec3(0.06) * step(0.985, max(abs(cos(lat * 12.0)), abs(cos(lon * 12.0))));\n"
    "  vec3 night = u_has_night > 0.5 ? texture2D(u_night, uv).rgb : vec3(0.0);\n"
    "  float d = dot(dir, u_sun);\n"
    "  float lit = smoothstep(-0.10, 0.15, d);\n"
    "  vec3 col = day * (0.05 + 0.95 * lit) * u_bright;\n"
    "  col += night * (1.0 - smoothstep(-0.20, 0.05, d)) * u_lights;\n"
    "  float rim = pow(1.0 - clamp(view.z, 0.0, 1.0), 3.0);\n"
    "  col += vec3(0.25, 0.5, 1.0) * rim * 0.35 * (0.3 + 0.7 * lit);\n"
    /* dark band behind the text across the equator (fixed, doesn't turn) */
    "  vec3 q = u_vinv * view;\n"
    "  float inband = smoothstep(u_bh + 0.012, u_bh, abs(q.y)) * smoothstep(u_bw + 0.06, u_bw, abs(atan(q.x, q.z)));\n"
    "  col *= 1.0 - u_band * inband;\n"
    "  return vec4(col, 1.0);\n"
    "}\n";

/* ---------------------------------------------------------------- */
/* Data from orbitfeed.py                                            */
/* ---------------------------------------------------------------- */
#define MAX_SATS   1200
#define MAX_TRACK  200
#define MAX_QUAKES 600

typedef struct { char code; float lat, lon, alt, vlat, vlon; char name[25]; } Sat;
typedef struct { float min, lat, lon; } TrackPt;
typedef struct { float mag, lat, lon; long t; char place[61]; } Quake;

static Sat     g_sat[MAX_SATS];  static int g_nsat;  static double g_sat_t;
static TrackPt g_trk[MAX_TRACK]; static int g_ntrk;  static double g_trk_t;
static Quake   g_qk[MAX_QUAKES]; static int g_nqk;
static time_t  g_mt_sats, g_mt_trk, g_mt_qk;
static int     g_warned;

static time_t mtime(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 ? st.st_mtime : 0;
}

static float wrap180(float x) { while (x > 180.0f) x -= 360.0f; while (x < -180.0f) x += 360.0f; return x; }

static void read_sats(void)
{
    const char *path = dome_path("data/sats.txt");
    FILE *f = fopen(path, "r");
    if (!f) return;
    static Sat prev[MAX_SATS];
    int nprev = g_nsat; double tprev = g_sat_t;
    memcpy(prev, g_sat, sizeof(Sat) * (size_t)nprev);
    char line[200];
    double t = 0;
    int n = 0;
    while (fgets(line, sizeof line, f) && n < MAX_SATS) {
        if (line[0] == 't') { t = atof(line + 2); continue; }
        Sat s; memset(&s, 0, sizeof s);
        if (sscanf(line, " %c %f %f %f %24s", &s.code, &s.lat, &s.lon, &s.alt, s.name) >= 4)
            g_sat[n++] = s;
    }
    fclose(f);
    g_nsat = n; g_sat_t = t;
    /* speeds from the previous update, matched by name */
    double dt = t - tprev;
    for (int i = 0; i < n; i++) {
        g_sat[i].vlat = g_sat[i].vlon = 0;
        if (dt <= 0 || dt > 60) continue;
        int j = (i < nprev && !strcmp(prev[i].name, g_sat[i].name)) ? i : -1;
        for (int k = 0; j < 0 && k < nprev; k++) if (!strcmp(prev[k].name, g_sat[i].name)) j = k;
        if (j < 0) continue;
        g_sat[i].vlat = (float)((g_sat[i].lat - prev[j].lat) / dt);
        g_sat[i].vlon = (float)(wrap180(g_sat[i].lon - prev[j].lon) / dt);
    }
}

static void read_track(void)
{
    FILE *f = fopen(dome_path("data/track.txt"), "r");
    if (!f) return;
    int n = 0;
    while (n < MAX_TRACK && fscanf(f, "%f %f %f", &g_trk[n].min, &g_trk[n].lat, &g_trk[n].lon) == 3) n++;
    fclose(f);
    g_ntrk = n;
    g_trk_t = (double)mtime(dome_path("data/track.txt"));
}

static void read_quakes(void)
{
    FILE *f = fopen(dome_path("data/quakes.txt"), "r");
    if (!f) return;
    char line[200];
    int n = 0;
    while (fgets(line, sizeof line, f) && n < MAX_QUAKES) {
        Quake q; memset(&q, 0, sizeof q);
        int used = 0;
        if (sscanf(line, "%f %f %f %ld %n", &q.mag, &q.lat, &q.lon, &q.t, &used) >= 4) {
            snprintf(q.place, sizeof q.place, "%s", line + used);
            q.place[strcspn(q.place, "\r\n")] = 0;
            g_qk[n++] = q;
        }
    }
    fclose(f);
    g_nqk = n;
}

static void poll_data(void)
{
    time_t a = mtime(dome_path("data/sats.txt"));
    time_t b = mtime(dome_path("data/track.txt"));
    time_t c = mtime(dome_path("data/quakes.txt"));
    if (a != g_mt_sats) { g_mt_sats = a; read_sats(); }
    if (b != g_mt_trk)  { g_mt_trk = b; read_track(); }
    if (c != g_mt_qk)   { g_mt_qk = c; read_quakes(); }
    int stale = !a || time(NULL) - a > 60;
    if (stale && !g_warned) {
        printf("Orbit watch: no fresh data in %s. Is orbitfeed running?\n", dome_path("data"));
        g_warned = 1;
    }
    if (!stale) g_warned = 0;
}

/* ISS position now, from the track (minute steps), else the last update */
static int iss_now(float *lat, float *lon)
{
    double now = (double)time(NULL);
    if (g_ntrk > 1) {
        double m = (now - g_trk_t) / 60.0;
        for (int i = 0; i + 1 < g_ntrk; i++)
            if (g_trk[i].min <= m && m < g_trk[i + 1].min) {
                float u = (float)((m - g_trk[i].min) / (g_trk[i + 1].min - g_trk[i].min));
                *lat = g_trk[i].lat + u * (g_trk[i + 1].lat - g_trk[i].lat);
                *lon = wrap180(g_trk[i].lon + u * wrap180(g_trk[i + 1].lon - g_trk[i].lon));
                return 1;
            }
    }
    for (int i = 0; i < g_nsat; i++)
        if (g_sat[i].code == 'I') { *lat = g_sat[i].lat; *lon = g_sat[i].lon; return 1; }
    return 0;
}

/* ---------------------------------------------------------------- */
/* Sun                                                               */
/* ---------------------------------------------------------------- */
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
    dome_sph((float)dec, (float)(ra - gmst), out);    /* point under the Sun */
}

/* ---------------------------------------------------------------- */
static GLuint     g_prog, g_day, g_night;
static int        g_has_day, g_has_night;
static DomePoints g_pts, g_iss, g_text;
static float      g_t, g_clat, g_clon;              /* current view centre */
static int        g_centre_set;
static time_t     g_text_built;

static int name_has(const char *n, const char *w)
{
    size_t a = strlen(n), b = strlen(w);
    for (size_t i = 0; i + b <= a; i++) if (!strncasecmp(n + i, w, b)) return (int)i;
    return -1;
}

static int load_tex(const char *path, GLuint tex)
{
    int w, h, ch;
    unsigned char *img = stbi_load(path, &w, &h, &ch, 4);
    if (!img) return 0;
    GLint max = 2048; glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max);
    if (w > max || h > max) { stbi_image_free(img); printf("Orbit watch: %s too big\n", path); return 0; }
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
    stbi_image_free(img);
    return 1;
}

static void load_earth(void)
{
    DIR *d = opendir(dome_path("textures"));
    char day[300] = "", any[300] = "";
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            if (name_has(e->d_name, "earth") < 0 || name_has(e->d_name, "night") >= 0) continue;
            if (name_has(e->d_name, "day") >= 0) snprintf(day, sizeof day, "%s", e->d_name);
            else if (!any[0] && (name_has(e->d_name, ".jpg") >= 0 || name_has(e->d_name, ".png") >= 0))
                snprintf(any, sizeof any, "%s", e->d_name);
        }
        closedir(d);
    }
    const char *pick = day[0] ? day : any;
    char path[600];
    if (pick[0]) {
        snprintf(path, sizeof path, "%s/%s", dome_path("textures"), pick);
        g_has_day = load_tex(path, g_day);
        int at = name_has(pick, "day");
        if (at >= 0) {
            snprintf(path, sizeof path, "%s/%.*snight%s", dome_path("textures"), at, pick, pick + at + 3);
            g_has_night = load_tex(path, g_night);
        }
    }
    printf("Orbit watch: %s%s\n", g_has_day ? pick : "no Earth map, plain globe",
           g_has_night ? " with night lights" : "");
}

static void tex_setup(GLuint *t)
{
    glGenTextures(1, t);
    glBindTexture(GL_TEXTURE_2D, *t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

static void init(void)
{
    g_prog = dome_surface_program(EARTH_GLSL);
    tex_setup(&g_day);
    tex_setup(&g_night);
    load_earth();
    dome_points_init(&g_pts, 120000);
    dome_points_init(&g_iss, 2000);
    dome_points_init(&g_text, 60000);
    poll_data();
}

static void update(const DomeFrame *f)
{
    g_t += f->dt;
    poll_data();
}

static void ll(float lat_deg, float lon_deg, float out[3])
{
    dome_sph(DEG2RAD(lat_deg), DEG2RAD(lon_deg), out);
}

/* a ring of dots round a point on the globe, radius in degrees */
static void ring(DomePoints *p, float lat, float lon, float rad_deg, int n,
                 float r, float g, float b, float a)
{
    float c[3]; ll(lat, lon, c);
    float e[3] = { c[2], 0.0f, -c[0] };                 /* east  */
    float el = sqrtf(e[0] * e[0] + e[2] * e[2]) + 1e-6f;
    e[0] /= el; e[2] /= el;
    float nn[3] = { c[1] * e[2] - c[2] * e[1], c[2] * e[0] - c[0] * e[2], c[0] * e[1] - c[1] * e[0] };
    float s = sinf(DEG2RAD(rad_deg)), co = cosf(DEG2RAD(rad_deg));
    for (int i = 0; i < n; i++) {
        float t = 6.2832f * i / n, ct = cosf(t), st = sinf(t);
        dome_points_push(p, c[0] * co + s * (ct * e[0] + st * nn[0]),
                            c[1] * co + s * (ct * e[1] + st * nn[1]),
                            c[2] * co + s * (ct * e[2] + st * nn[2]), r, g, b, a);
    }
}

/* is this point (Earth frame) behind the text band right now? */
static float  g_band_w = 0.6f, g_band_h = 0.2f;
static int    g_band_on;
static mat3   g_band_rot, g_band_inv;
static int in_band(const float v[3])
{
    if (!g_band_on) return 0;
    float d[3], q[3];
    for (int i = 0; i < 3; i++)
        d[i] = g_band_rot[i] * v[0] + g_band_rot[3 + i] * v[1] + g_band_rot[6 + i] * v[2];
    for (int i = 0; i < 3; i++)
        q[i] = g_band_inv[i] * d[0] + g_band_inv[3 + i] * d[1] + g_band_inv[6 + i] * d[2];
    return fabsf(q[1]) < g_band_h + 0.01f && q[2] > 0.0f && fabsf(atan2f(q[0], q[2])) < g_band_w + 0.05f;
}

static void build_text(void)
{
    char l1[64], l2[64], l3[64], l4[64];
    float lat, lon;
    int have = iss_now(&lat, &lon);
    int stale = !g_mt_sats || time(NULL) - g_mt_sats > 60;
    float alt = 0, kmh = 0;
    for (int i = 0; i < g_nsat; i++) if (g_sat[i].code == 'I') alt = g_sat[i].alt;
    if (g_ntrk > 16) {        /* speed from one minute of track */
        float a[3], b[3];
        ll(g_trk[15].lat, g_trk[15].lon, a); ll(g_trk[16].lat, g_trk[16].lon, b);
        float ang = acosf(fminf(1.0f, a[0] * b[0] + a[1] * b[1] + a[2] * b[2]));
        kmh = ang * (6371.0f + alt) * 60.0f;
    }
    float big = 0; for (int i = 0; i < g_nqk; i++) if (g_qk[i].mag > big) big = g_qk[i].mag;
    if (stale) {
        snprintf(l1, sizeof l1, "NO LIVE DATA");
        snprintf(l2, sizeof l2, "IS ORBITFEED");
        snprintf(l3, sizeof l3, "RUNNING?");
    } else if (have) {
        snprintf(l1, sizeof l1, "ISS %.1f%c %.1f%c",
                 fabsf(lat), lat >= 0 ? 'N' : 'S', fabsf(lon), lon >= 0 ? 'E' : 'W');
        snprintf(l2, sizeof l2, "%.0f KM UP", alt);
        snprintf(l3, sizeof l3, "%.0f KM/H", kmh);
    } else {
        snprintf(l1, sizeof l1, "ISS NOT IN");
        snprintf(l2, sizeof l2, "THE DATA");
        snprintf(l3, sizeof l3, "%d SATELLITES", g_nsat);
    }
    if (g_nqk) snprintf(l4, sizeof l4, "%d QUAKES MAX M%.1f", g_nqk, big);
    else snprintf(l4, sizeof l4, "%d SATELLITES", g_nsat);
    const char *lines[4] = { l1, l2, l3, l4 };
    static const float COL[4][3] = { { 0.55f, 1.0f, 1.0f }, { 0.9f, 0.95f, 1.0f },
                                     { 0.9f, 0.95f, 1.0f }, { 1.0f, 0.85f, 0.45f } };
    dome_points_clear(&g_text);
    g_band_w = dome_text_block(&g_text, lines, COL, 4, 4.6f, &g_band_h);
    dome_points_upload(&g_text);
    g_text_built = time(NULL);
}

static void draw(const DomeFrame *f)
{
    /* where the view is centred */
    mat3 rot;
    int mode = (int)lroundf(L.view);
    float tlat = 0, tlon = 0, ilat, ilon;
    int have_iss = iss_now(&ilat, &ilon);
    if (mode == 1 && have_iss) { tlat = ilat; tlon = ilon; }
    else if (mode == 2)        { tlat = L.home_lat; tlon = L.home_lon; }
    if (mode == 0 || (mode == 1 && !have_iss)) {
        memcpy(rot, f->rot, sizeof rot);
        g_centre_set = 0;
    } else {
        if (!g_centre_set) { g_clat = tlat; g_clon = tlon; g_centre_set = 1; }
        float k = 1.0f - expf(-f->dt * 2.0f);          /* glide to a new centre */
        g_clat += (tlat - g_clat) * k;
        g_clon = wrap180(g_clon + wrap180(tlon - g_clon) * k);
        mat3 rx, ry;
        m3_rx(rx, DEG2RAD(g_clat));
        m3_ry(ry, -DEG2RAD(g_clon));
        m3_mul(rot, rx, ry);
    }

    /* the Earth */
    float sun[3]; sun_dir(sun);
    GLuint p = g_prog;
    DomeFrame ff = *f;
    memcpy(ff.rot, rot, sizeof rot);
    dome_surface_begin(p, &ff);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, g_day);
    glUniform1i(glGetUniformLocation(p, "u_day"), 1);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, g_night);
    glUniform1i(glGetUniformLocation(p, "u_night"), 2);
    glActiveTexture(GL_TEXTURE0);
    glUniform1f(glGetUniformLocation(p, "u_has_day"), (float)g_has_day);
    glUniform1f(glGetUniformLocation(p, "u_has_night"), (float)g_has_night);
    glUniform1f(glGetUniformLocation(p, "u_lights"), L.lights);
    glUniform1f(glGetUniformLocation(p, "u_bright"), L.bright);
    glUniform3fv(glGetUniformLocation(p, "u_sun"), 1, sun);
    mat3 vrot, vinv;
    dome_view_rot(vrot);
    m3_transpose(vinv, vrot);
    glUniformMatrix3fv(glGetUniformLocation(p, "u_vinv"), 1, GL_FALSE, vinv);
    glUniform1f(glGetUniformLocation(p, "u_band"), L.text > 0.5f ? L.band : 0.0f);
    glUniform1f(glGetUniformLocation(p, "u_bw"), g_band_w);
    glUniform1f(glGetUniformLocation(p, "u_bh"), g_band_h);
    dome_surface_end();

    /* quakes, satellites and the ISS track; satellites and quakes behind
     * the text band are left out so the text stays clear */
    g_band_on = L.text > 0.5f;
    memcpy(g_band_rot, rot, sizeof rot);
    memcpy(g_band_inv, vinv, sizeof vinv);
    double now = (double)time(NULL);
    dome_points_clear(&g_pts);
    if (L.quakes > 0.0f)
        for (int i = 0; i < g_nqk; i++) {
            Quake *q = &g_qk[i];
            float qv[3]; ll(q->lat, q->lon, qv);
            if (in_band(qv)) continue;
            float age_h = (float)((now - q->t) / 3600.0);
            float fresh = age_h < 6.0f ? 1.0f : 0.55f;
            float r = 1.0f, g = q->mag < 4.0f ? 0.85f : q->mag < 6.0f ? 0.5f : 0.15f;
            float b = q->mag < 4.0f ? 0.3f : 0.1f;
            float size = 0.35f * q->mag;                    /* degrees */
            float ph = fmodf(g_t * 0.4f + i * 0.37f, 1.0f); /* each ring pulses outwards */
            ring(&g_pts, q->lat, q->lon, size * (0.3f + 1.2f * ph), 24 + (int)(q->mag * 4),
                 r, g, b, (1.0f - ph) * fresh * L.quakes);
            ring(&g_pts, q->lat, q->lon, size * 0.25f, 8, r, g, b, fresh * L.quakes);
        }
    if (L.sats > 0.0f) {
        float dt = (float)(now - g_sat_t);
        if (dt > 30.0f) dt = 30.0f;
        for (int i = 0; i < g_nsat; i++) {
            Sat *s = &g_sat[i];
            if (s->code == 'I') continue;
            float c[3] = { 1, 1, 1 };
            if (s->code == 'S') { c[0] = 1.0f; c[1] = 0.85f; c[2] = 0.2f; }
            else if (s->code == 'G') { c[0] = 0.3f; c[1] = 1.0f; c[2] = 0.4f; }
            else if (s->code == 'W') { c[0] = 0.4f; c[1] = 0.7f; c[2] = 1.0f; }
            float v[3]; ll(s->lat + s->vlat * dt, wrap180(s->lon + s->vlon * dt), v);
            if (in_band(v)) continue;
            dome_points_push(&g_pts, v[0], v[1], v[2], c[0], c[1], c[2], 0.9f * L.sats);
        }
    }
    /* ISS path: next orbit ahead, short fading trail behind */
    double m_now = (now - g_trk_t) / 60.0;
    for (int i = 0; i + 1 < g_ntrk; i++) {
        float a[3], b[3];
        ll(g_trk[i].lat, g_trk[i].lon, a); ll(g_trk[i + 1].lat, g_trk[i + 1].lon, b);
        if (fabsf(g_trk[i + 1].lon - g_trk[i].lon) > 180.0f) { memcpy(b, a, sizeof b); }
        float m = g_trk[i].min;
        int ahead = m >= m_now;
        float al = ahead ? 0.85f * (1.0f - (float)(m - m_now) / 110.0f) : 0.8f * (1.0f + (float)(m - m_now) / 15.0f);
        if (al <= 0.02f) continue;
        if (ahead) dome_points_line(&g_pts, a, b, 0.3f, 0.95f, 1.0f, al, 0.008f);
        else       dome_points_line(&g_pts, a, b, 1.0f, 1.0f, 1.0f, al, 0.004f);
    }
    dome_points_upload(&g_pts);
    float none[3] = { 0, 0, 1 };
    dome_points_aperture(none, -2.0f);
    dome_draw_points(&g_pts, rot, f->fade, 3.0f);

    /* the ISS itself: bright dot with a pulsing halo */
    if (have_iss) {
        dome_points_clear(&g_iss);
        float v[3]; ll(ilat, ilon, v);
        dome_points_push(&g_iss, v[0], v[1], v[2], 1.0f, 1.0f, 1.0f, 1.0f);
        float ph = fmodf(g_t * 0.7f, 1.0f);
        ring(&g_iss, ilat, ilon, 0.8f + 2.5f * ph, 36, 0.4f, 1.0f, 1.0f, 1.0f - ph);
        dome_points_upload(&g_iss);
        dome_draw_points(&g_iss, rot, f->fade, 4.0f);
        dome_draw_points(&g_iss, rot, f->fade * 0.4f, 12.0f);    /* glow */
    }

    if (L.text > 0.5f) {
        if (time(NULL) != g_text_built) build_text();
        mat3 vr; dome_view_rot(vr);
        dome_draw_points(&g_text, vr, f->fade, 2.5f);
    }
}

static int key(SDL_Keycode k, int shift)
{
    (void)shift;
    if (k != SDLK_n) return 0;
    L.view = (float)(((int)lroundf(L.view) + 1) % 3);
    static const char *NAMES[3] = { "turning", "following the ISS", "over home" };
    printf("Orbit watch: %s\n", NAMES[(int)L.view]);
    return 1;
}

Scene scene_orbit = {
    "ORBIT WATCH", "orbit.cfg", 0, P, (int)(sizeof P / sizeof P[0]),
    init, NULL, update, draw, key,
    "N cycles the view: turning, follow the ISS, over home", NULL
};
