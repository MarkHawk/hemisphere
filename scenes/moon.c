/*
 * moon.c  -  the Moon tonight.
 *
 * Shows the Moon as it is right now, filling the whole dome: lit to the
 * real phase from the Pi's clock, with a faint earthshine on the dark
 * side. Three lines of text across the equator, on a dark band so they
 * read over the surface, give the phase name, how much is lit, the
 * Moon's age, and the dates of the next full and new moon in local time.
 * No internet needed. The Moon follows the view settings (pitch, tilt)
 * so north is at the top of the globe as it sits on your desk.
 *
 * Uses a moon map from the textures folder (any file with "moon" in its
 * name, such as 2k_moon.jpg). Without one it draws a plain grey moon.
 *
 * Astronomy from Jean Meeus, "Astronomical Algorithms": phase angle from
 * chapter 48 and the times of new and full moon from chapter 49 (main
 * terms, good to a few minutes). The lit side faces the right way for
 * the northern hemisphere: on the right while waxing, the left while
 * waning.
 */
#include "../dome/dome.h"
#include "../third_party/stb_image.h"
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

static struct { float shine, rock, text, band, bright; } L =
    { 0.06f, 5.0f, 1.0f, 0.65f, 1.0f };

static Param P[] = {
    { "shine", &L.shine, 0.01f, 0.0f, 0.30f, 0, "earthshine: how much the dark side shows" },
    { "rock",  &L.rock,  0.5f,  0.0f, 10.0f, 0, "slow rocking (libration), degrees" },
    { "text",  &L.text,  1.0f,  0.0f, 1.0f, 0, "show the phase text: 0 off, 1 on" },
    { "band",  &L.band,  0.05f, 0.0f, 1.0f, 0, "darkness of the band behind the text" },
    { "bright",&L.bright,0.05f, 0.2f, 2.0f, 0, "brightness" },
};

static const char *MOON_GLSL =
    "uniform sampler2D u_tex;\n"
    "uniform float u_has_tex, u_shine, u_rock, u_bright, u_band, u_bw, u_bh;\n"
    "uniform vec3  u_light;\n"
    "vec4 surface(vec3 dir, vec3 view){\n"
    /* the Moon painted on the globe, facing you: dir is in the globe's own
     * frame (+Y north, +Z towards you), so it is also the surface normal */
    "  vec3 n = dir;\n"
    "  float lat = asin(clamp(n.y, -1.0, 1.0));\n"
    "  float lon = atan(n.x, n.z);\n"
    "  float tl = lon + u_rock;\n"
    "  vec3 c = u_has_tex > 0.5\n"
    "      ? texture2D(u_tex, vec2(tl / (2.0 * PI) + 0.5, 0.5 - lat / PI)).rgb\n"
    "      : vec3(0.62, 0.61, 0.58) * (0.9 + 0.1 * sin(tl * 7.0) * sin(lat * 5.0));\n"
    "  float d = dot(n, u_light);\n"
    "  float lit = smoothstep(-0.03, 0.07, d);\n"
    "  float shade = lit * (0.85 + 0.15 * max(d, 0.0)) + (1.0 - lit) * u_shine;\n"
    "  vec3 col = c * shade * u_bright;\n"
    /* dark band behind the text across the equator */
    "  float inband = smoothstep(u_bh + 0.012, u_bh, abs(n.y)) * smoothstep(u_bw + 0.06, u_bw, abs(lon));\n"
    "  col *= 1.0 - u_band * inband;\n"
    "  return vec4(col, 1.0);\n"
    "}\n";

/* ---------------------------------------------------------------- */
/* Astronomy                                                         */
/* ---------------------------------------------------------------- */
#define D2R (3.14159265358979 / 180.0)

/* DOME_DAYS=n shifts the clock by n days, to preview another night */
static double jd_now(void)
{
    const char *d = getenv("DOME_DAYS");
    return (double)time(NULL) / 86400.0 + 2440587.5 + (d ? atof(d) : 0.0);
}
static time_t jd_to_unix(double jd) { return (time_t)((jd - 2440587.5) * 86400.0); }

/* phase angle in degrees: 0 full, 180 new (Meeus 48.4) */
static double phase_angle(double jd)
{
    double T = (jd - 2451545.0) / 36525.0;
    double D = 297.8501921 + 445267.1114034 * T;
    double M = 357.5291092 + 35999.0502909 * T;
    double Mp = 134.9633964 + 477198.8675055 * T;
    return 180.0 - D - 6.289 * sin(Mp * D2R) + 2.100 * sin(M * D2R)
           - 1.274 * sin((2 * D - Mp) * D2R) - 0.658 * sin(2 * D * D2R)
           - 0.214 * sin(2 * Mp * D2R) - 0.110 * sin(D * D2R);
}

/* time of the new (full = 0) or full (full = 1) moon number k (Meeus 49) */
static double phase_jde(double k, int full)
{
    double T = k / 1236.85;
    double jde = 2451550.09766 + 29.530588861 * k + 0.00015437 * T * T
                 - 0.000000150 * T * T * T;
    double E = 1.0 - 0.002516 * T - 0.0000074 * T * T;
    double M = (2.5534 + 29.10535670 * k) * D2R;
    double Mp = (201.5643 + 385.81693528 * k + 0.0107582 * T * T) * D2R;
    double F = (160.7108 + 390.67050284 * k - 0.0016118 * T * T) * D2R;
    double c = full
        ? -0.40614 * sin(Mp) + 0.17302 * E * sin(M) + 0.01614 * sin(2 * Mp)
          + 0.01043 * sin(2 * F) + 0.00734 * E * sin(Mp - M) - 0.00515 * E * sin(Mp + M)
          + 0.00209 * E * E * sin(2 * M) - 0.00111 * sin(Mp - 2 * F)
          - 0.00057 * sin(Mp + 2 * F) + 0.00056 * E * sin(2 * Mp + M)
          - 0.00042 * sin(3 * Mp) + 0.00042 * E * sin(M + 2 * F)
          + 0.00038 * E * sin(M - 2 * F) - 0.00024 * E * sin(2 * Mp - M)
        : -0.40720 * sin(Mp) + 0.17241 * E * sin(M) + 0.01608 * sin(2 * Mp)
          + 0.01039 * sin(2 * F) + 0.00739 * E * sin(Mp - M) - 0.00514 * E * sin(Mp + M)
          + 0.00208 * E * E * sin(2 * M) - 0.00111 * sin(Mp - 2 * F)
          - 0.00057 * sin(Mp + 2 * F) + 0.00056 * E * sin(2 * Mp + M)
          - 0.00042 * sin(3 * Mp) + 0.00042 * E * sin(M + 2 * F)
          + 0.00038 * E * sin(M - 2 * F) - 0.00024 * E * sin(2 * Mp - M);
    return jde + c - 69.0 / 86400.0;          /* TT to UT, roughly */
}

/* the first new or full moon after jd (after = 1), or the last one before it */
static double find_phase(double jd, int full, int after)
{
    double k = floor((jd - 2451550.09766) / 29.530588861) - 2.0 + (full ? 0.5 : 0.0);
    double prev = phase_jde(k, full);
    for (int i = 0; i < 6; i++) {
        double j = phase_jde(k + 1.0, full);
        if (j > jd) return after ? j : prev;
        prev = j; k += 1.0;
    }
    return prev;
}

/* ---------------------------------------------------------------- */
static GLuint     g_prog, g_tex;
static int        g_has_tex;
static DomePoints g_text;
static float      g_band_w, g_band_h;      /* text band half-width (rad), half-height (sin) */
#define TEXT_H 4.6f                         /* letter height, degrees */
static float      g_light[3], g_t;
static time_t     g_text_built;

static void load_moon_map(void)
{
    DIR *d = opendir(dome_path("textures"));
    if (!d) return;
    struct dirent *e;
    char path[600] = "";
    while ((e = readdir(d))) {
        size_t n = strlen(e->d_name);
        for (size_t i = 0; i + 4 <= n; i++)
            if (!strncasecmp(e->d_name + i, "moon", 4)) {
                snprintf(path, sizeof path, "%s/%s", dome_path("textures"), e->d_name);
                break;
            }
        if (path[0]) break;
    }
    closedir(d);
    if (!path[0]) { printf("Moon: no moon map in textures, drawing a plain one\n"); return; }
    int w, h, ch;
    unsigned char *img = stbi_load(path, &w, &h, &ch, 4);
    if (!img) { printf("Moon: cannot read %s\n", path); return; }
    GLint max = 2048; glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max);
    if (w > max || h > max) { printf("Moon: %s is too big (run prep_textures.py)\n", path); stbi_image_free(img); return; }
    glBindTexture(GL_TEXTURE_2D, g_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
    stbi_image_free(img);
    g_has_tex = 1;
    printf("Moon: %s\n", strrchr(path, '/') + 1);
}

static void upcase(char *s) { for (; *s; s++) if (*s >= 'a' && *s <= 'z') *s -= 32; }

static void build_text(void)
{
    double jd = jd_now();
    double i = fmod(phase_angle(jd), 360.0);
    if (i < 0) i += 360.0;
    double lit = (1.0 + cos(i * D2R)) / 2.0;
    double last_new = find_phase(jd, 0, 0);
    double age = jd - last_new;
    int waxing = age < 29.530588861 / 2.0;

    /* light direction: from the viewer's side at full, from behind at new;
     * lit on the right while waxing, on the left while waning */
    double s = sin(i * D2R), c = cos(i * D2R);
    g_light[0] = (float)(waxing ? fabs(s) : -fabs(s));
    g_light[1] = 0.0f;
    g_light[2] = (float)c;

    const char *name =
        age < 1.0  ? "NEW MOON" : age < 6.4  ? "WAXING CRESCENT" :
        age < 8.4  ? "FIRST QUARTER" : age < 13.8 ? "WAXING GIBBOUS" :
        age < 15.8 ? "FULL MOON" : age < 21.1 ? "WANING GIBBOUS" :
        age < 23.1 ? "LAST QUARTER" : age < 28.5 ? "WANING CRESCENT" : "NEW MOON";

    char top[64], line1[64], line2[64], line3[64], full_s[32], new_s[32];
    time_t tf = jd_to_unix(find_phase(jd, 1, 1)), tn = jd_to_unix(find_phase(jd, 0, 1));
    strftime(full_s, sizeof full_s, "%d %b %H:%M", localtime(&tf));
    strftime(new_s, sizeof new_s, "%d %b %H:%M", localtime(&tn));
    snprintf(top, sizeof top, "%s %d%%", name, (int)lround(lit * 100.0));
    snprintf(line1, sizeof line1, "AGE %.1f DAYS", age);
    snprintf(line2, sizeof line2, "FULL %s", full_s);
    snprintf(line3, sizeof line3, "NEW  %s", new_s);
    upcase(line2); upcase(line3);

    const char *lines[4] = { top, line1, line2, line3 };
    static const float COL[4][3] = { { 1.0f, 0.95f, 0.75f }, { 0.9f, 0.92f, 1.0f },
                                     { 0.8f, 0.88f, 1.0f }, { 0.8f, 0.88f, 1.0f } };
    dome_points_clear(&g_text);
    g_band_w = dome_text_block(&g_text, lines, COL, 4, TEXT_H, &g_band_h);
    dome_points_upload(&g_text);
    g_text_built = time(NULL);
    printf("Moon: %s, %.0f%% lit, age %.1f days; full %s, new %s\n",
           name, lit * 100.0, age, full_s, new_s);
}

static void init(void)
{
    g_prog = dome_surface_program(MOON_GLSL);
    glGenTextures(1, &g_tex);
    glBindTexture(GL_TEXTURE_2D, g_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    load_moon_map();

    dome_points_init(&g_text, 60000);
    build_text();
}

static void rebuild(void) { build_text(); }

static void update(const DomeFrame *f)
{
    g_t += f->dt;
    if (time(NULL) - g_text_built >= 60) build_text();
}

static void draw(const DomeFrame *f)
{
    /* the Moon doesn't spin: it faces you, the right way up */
    DomeFrame ff = *f;
    dome_view_rot(ff.rot);

    GLuint p = g_prog;
    dome_surface_begin(p, &ff);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, g_tex);
    glUniform1i(glGetUniformLocation(p, "u_tex"), 1);
    glUniform1f(glGetUniformLocation(p, "u_has_tex"), (float)g_has_tex);
    glUniform1f(glGetUniformLocation(p, "u_band"), L.text > 0.5f ? L.band : 0.0f);
    glUniform1f(glGetUniformLocation(p, "u_bw"), g_band_w);
    glUniform1f(glGetUniformLocation(p, "u_bh"), g_band_h);
    glUniform1f(glGetUniformLocation(p, "u_shine"), L.shine);
    glUniform1f(glGetUniformLocation(p, "u_rock"), DEG2RAD(L.rock) * sinf(g_t * 0.05f));
    glUniform1f(glGetUniformLocation(p, "u_bright"), L.bright);
    glUniform3fv(glGetUniformLocation(p, "u_light"), 1, g_light);
    glActiveTexture(GL_TEXTURE0);
    dome_surface_end();

    if (L.text > 0.5f) dome_draw_points(&g_text, ff.rot, f->fade, 2.5f);
}

Scene scene_moon = {
    "MOON TONIGHT", "moon.cfg", 0, P, (int)(sizeof P / sizeof P[0]),
    init, rebuild, update, draw, NULL, NULL, NULL
};
